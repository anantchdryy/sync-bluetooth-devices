#include "DesktopAudioClient.hpp"
#include "OutputLatency.hpp"

#include "ClockSyncTransport.hpp"
#include "DriftCorrection.hpp"
#include "JitterBuffer.hpp"
#include "PacketSerializer.hpp"
#include "PlaybackClock.hpp"
#include "UdpAudioReceiver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <miniaudio.h>

namespace {

using namespace std::chrono_literals;

class PacketReceiverWorker {
public:
  PacketReceiverWorker(const DesktopAudioClientConfig &config,
                       JitterBuffer &jitterBuffer)
      : receiver_(config.bindAddress, config.audioPort, 100ms),
        jitterBuffer_(jitterBuffer), thread_(&PacketReceiverWorker::run, this) {
  }

  ~PacketReceiverWorker() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  PacketReceiverWorker(const PacketReceiverWorker &) = delete;
  PacketReceiverWorker &operator=(const PacketReceiverWorker &) = delete;

  void rethrowIfFailed() const {
    std::scoped_lock lock(exceptionMutex_);
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }

  [[nodiscard]] std::uint64_t packetsReceived() const noexcept {
    return packetsReceived_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::uint64_t packetsLost() const noexcept {
    return packetsLost_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::uint64_t outOfOrderPackets() const noexcept {
    return outOfOrderPackets_.load(std::memory_order_acquire);
  }

  [[nodiscard]] PlaybackClock::Timestamp lastPacketTime() const noexcept {
    return PlaybackClock::Timestamp{PlaybackClock::Duration{
        lastPacketNanoseconds_.load(std::memory_order_acquire)}};
  }

private:
  void updateSequenceStatistics(std::uint32_t sequence) {
    if (!highestSequence_) {
      highestSequence_ = sequence;
      return;
    }
    if (sequence > *highestSequence_) {
      const auto gap = static_cast<std::uint64_t>(sequence) -
                       static_cast<std::uint64_t>(*highestSequence_) - 1U;
      packetsLost_.fetch_add(gap, std::memory_order_release);
      if (gap <= 100'000) {
        for (auto missing = *highestSequence_ + 1U; missing < sequence;
             ++missing) {
          missingSequences_.insert(missing);
        }
      }
      highestSequence_ = sequence;
      return;
    }

    outOfOrderPackets_.fetch_add(1, std::memory_order_release);
    if (missingSequences_.erase(sequence) != 0) {
      packetsLost_.fetch_sub(1, std::memory_order_release);
    }
  }

  void run() noexcept {
    try {
      while (!stop_.load(std::memory_order_acquire)) {
        const auto datagram = receiver_.receive();
        if (!datagram) {
          continue;
        }
        AudioPacket packet;
        try {
          packet = PacketSerializer::deserialize(*datagram);
        } catch (const std::invalid_argument &) {
          continue;
        }
        if (!sessionId_) {
          sessionId_ = packet.sessionId;
          streamId_ = packet.streamId;
        } else if (packet.sessionId != *sessionId_) {
          continue;
        }
        if (packet.streamId != *streamId_) continue;
        updateSequenceStatistics(packet.sequenceNumber);
        bool inserted = false;
        try {
          inserted = jitterBuffer_.push(packet);
        } catch (const std::invalid_argument &) {
          continue;
        }
        if (inserted) {
          packetsReceived_.fetch_add(1, std::memory_order_release);
          lastPacketNanoseconds_.store(
              PlaybackClock::now().time_since_epoch().count(),
              std::memory_order_release);
        }
      }
    } catch (...) {
      std::scoped_lock lock(exceptionMutex_);
      exception_ = std::current_exception();
      stop_.store(true, std::memory_order_release);
    }
  }

  UdpAudioReceiver receiver_;
  JitterBuffer &jitterBuffer_;
  std::atomic_bool stop_{false};
  std::thread thread_;
  std::optional<std::uint64_t> sessionId_;
  std::optional<std::uint32_t> streamId_;
  std::optional<std::uint32_t> highestSequence_;
  std::unordered_set<std::uint32_t> missingSequences_;
  std::atomic<std::uint64_t> packetsReceived_{0};
  std::atomic<std::uint64_t> packetsLost_{0};
  std::atomic<std::uint64_t> outOfOrderPackets_{0};
  std::atomic<PlaybackClock::Duration::rep> lastPacketNanoseconds_{0};
  mutable std::mutex exceptionMutex_;
  std::exception_ptr exception_;
};

class ScheduledStreamPlayer {
public:
  ScheduledStreamPlayer(JitterBuffer &jitterBuffer,
                        const JitterBufferFormat &format,
                        std::chrono::milliseconds configuredDelay)
      : jitterBuffer_(jitterBuffer), channelCount_(format.channelCount),
        configuredDelay_(configuredDelay) {
    // Allocate outside the audio callback. Each rendering chunk needs at most
    // 259 source frames at the bounded correction rate.
    sourceSamples_.resize(260U * channelCount_);
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = format.channelCount;
    config.sampleRate = format.sampleRate;
    config.periodSizeInMilliseconds = 10;
    config.dataCallback = &ScheduledStreamPlayer::dataCallback;
    config.notificationCallback = &ScheduledStreamPlayer::notificationCallback;
    config.pUserData = this;
    const auto result = ma_device_init(nullptr, &config, &device_);
    if (result != MA_SUCCESS) {
      throw std::runtime_error(
          std::string("Unable to initialize client audio: ") +
          ma_result_description(result));
    }
    initialized_ = true;
    ma_device_info deviceInfo{};
    if (ma_device_get_info(&device_, ma_device_type_playback,
                           &deviceInfo) == MA_SUCCESS)
      outputRouteName_ = deviceInfo.name;
    else outputRouteName_ = "Unknown output";
  }

  ~ScheduledStreamPlayer() { stop(); }

  void schedule(std::uint64_t startFrame, PlaybackClock::Timestamp startTime) {
    currentFrame_.store(startFrame, std::memory_order_release);
    sourceFrame_ = startFrame;
    startTime_ = startTime;
    scheduled_ = true;
  }

  void start() {
    if (!scheduled_) {
      throw std::logic_error("Client audio device has not been scheduled");
    }
    const auto result = ma_device_start(&device_);
    if (result != MA_SUCCESS) {
      throw std::runtime_error(std::string("Unable to start client audio: ") +
                               ma_result_description(result));
    }
  }

  void stop() noexcept {
    if (initialized_) {
      ma_device_uninit(&device_);
      initialized_ = false;
    }
  }

  [[nodiscard]] std::uint64_t currentFrame() const noexcept {
    return currentFrame_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool started() const noexcept {
    return started_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::chrono::nanoseconds
  estimatedPlaybackDelay() const noexcept {
    return configuredDelay_ +
           std::chrono::nanoseconds{
               startLatenessNanoseconds_.load(std::memory_order_acquire)};
  }

  [[nodiscard]] std::uint64_t underrunFrames() const noexcept {
    return underrunFrames_.load(std::memory_order_acquire);
  }

  [[nodiscard]] const std::string &outputRouteName() const noexcept {
    return outputRouteName_;
  }

  [[nodiscard]] bool outputRouteChanged() const noexcept {
    return outputRouteChanged_.load(std::memory_order_acquire);
  }

  void setCorrectionRatio(double ratio) noexcept {
    targetRatio_.store(ratio, std::memory_order_release);
  }

  [[nodiscard]] double correctionRatio() const noexcept {
    return actualRatio_.load(std::memory_order_acquire);
  }

private:
  static void notificationCallback(const ma_device_notification *notification) {
    if (notification->type == ma_device_notification_type_rerouted) {
      auto *self = static_cast<ScheduledStreamPlayer *>(notification->pDevice->pUserData);
      self->outputRouteChanged_.store(true, std::memory_order_release);
    }
  }
  static void dataCallback(ma_device *device, void *output, const void *,
                           ma_uint32 frameCount) {
    auto *self = static_cast<ScheduledStreamPlayer *>(device->pUserData);
    const auto sampleCount =
        static_cast<std::size_t>(frameCount) * self->channelCount_;
    auto samples = std::span<std::int16_t>(static_cast<std::int16_t *>(output),
                                           sampleCount);
    std::fill(samples.begin(), samples.end(), std::int16_t{0});

    const auto currentTime = PlaybackClock::now();
    if (!self->started_.load(std::memory_order_acquire)) {
      if (currentTime < self->startTime_) {
        return;
      }
      self->startLatenessNanoseconds_.store(
          (currentTime - self->startTime_).count(), std::memory_order_release);
      self->started_.store(true, std::memory_order_release);
    }

    const double target = self->targetRatio_.load(std::memory_order_acquire);
    // Limit changes per callback to avoid abrupt pitch or phase jumps.
    self->ratio_ += std::clamp(target - self->ratio_, -0.00002, 0.00002);
    self->actualRatio_.store(self->ratio_, std::memory_order_release);
    for (ma_uint32 outputStart = 0; outputStart < frameCount;) {
      constexpr ma_uint32 chunkLimit = 256;
      const auto chunkFrames = std::min(chunkLimit, frameCount - outputStart);
      const auto currentFrame = self->sourceFrame_;
      const auto sourceFrames = static_cast<std::size_t>(
          std::floor(self->sourceFraction_ + chunkFrames * self->ratio_)) + 2U;
      const auto copiedFrames = self->jitterBuffer_.readFrames(
          currentFrame,
          std::span<std::int16_t>(self->sourceSamples_.data(),
                                  sourceFrames * self->channelCount_),
          true);
      for (ma_uint32 outputFrame = 0; outputFrame < chunkFrames; ++outputFrame) {
        const double sourcePosition =
            self->sourceFraction_ + outputFrame * self->ratio_;
        const auto index = static_cast<std::size_t>(sourcePosition);
        const double fraction = sourcePosition - static_cast<double>(index);
        for (std::uint16_t channel = 0; channel < self->channelCount_; ++channel) {
          const auto left =
              self->sourceSamples_[index * self->channelCount_ + channel];
          const auto right = self->sourceSamples_[(index + 1U) *
                                                  self->channelCount_ + channel];
          samples[(static_cast<std::size_t>(outputStart) + outputFrame) *
                      self->channelCount_ + channel] =
              static_cast<std::int16_t>(std::lround(
                  (1.0 - fraction) * left + fraction * right));
        }
      }
      const auto latestEndFrame = self->jitterBuffer_.latestEndFrame();
      const auto knownFrames = latestEndFrame > currentFrame
                                   ? std::min<std::uint64_t>(
                                         sourceFrames, latestEndFrame - currentFrame)
                                   : 0;
      if (copiedFrames < knownFrames) {
        self->underrunFrames_.fetch_add(knownFrames - copiedFrames,
                                        std::memory_order_release);
      }
      const double nextPosition =
          self->sourceFraction_ + chunkFrames * self->ratio_;
      const auto advancedFrames = static_cast<std::uint64_t>(nextPosition);
      self->sourceFrame_ += advancedFrames;
      self->sourceFraction_ = nextPosition - static_cast<double>(advancedFrames);
      outputStart += chunkFrames;
    }
    self->currentFrame_.store(self->sourceFrame_,
                              std::memory_order_release);
  }

  JitterBuffer &jitterBuffer_;
  std::uint16_t channelCount_{};
  std::uint64_t sourceFrame_{};
  double sourceFraction_{};
  double ratio_{1.0};
  std::vector<std::int16_t> sourceSamples_;
  std::string outputRouteName_;
  std::atomic<double> targetRatio_{1.0};
  std::atomic<double> actualRatio_{1.0};
  std::atomic<std::uint64_t> currentFrame_{};
  std::atomic_bool outputRouteChanged_{false};
  PlaybackClock::Timestamp startTime_{};
  std::chrono::milliseconds configuredDelay_{};
  ma_device device_{};
  bool initialized_{false};
  bool scheduled_{false};
  std::atomic_bool started_{false};
  std::atomic<PlaybackClock::Duration::rep> startLatenessNanoseconds_{0};
  std::atomic<std::uint64_t> underrunFrames_{0};
};

double milliseconds(std::chrono::nanoseconds duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

} // namespace

DesktopAudioClient::DesktopAudioClient(DesktopAudioClientConfig config)
    : config_(std::move(config)) {
  if (config_.audioPort == 0 || config_.clockSyncPort == 0 ||
      config_.playbackDelay < 0ms || config_.startupLead <= 0ms ||
      config_.outputLatencyAdjustment < -1s ||
      config_.outputLatencyAdjustment > 1s) {
    throw std::invalid_argument(
        "Desktop audio client configuration is invalid");
  }
}

DesktopAudioClientStats
DesktopAudioClient::run(const std::string &hostAddress) {
  JitterBuffer jitterBuffer;
  PacketReceiverWorker receiver(config_, jitterBuffer);
  const auto clockEstimate =
      ClockSyncClient::measure(hostAddress, config_.clockSyncPort);
  DriftEstimator driftEstimator;
  driftEstimator.addSample(clockEstimate.clientSampleTimestampNanoseconds,
                           clockEstimate.hostMinusClientOffset.count());
  GradualDriftCorrector driftCorrector;
  auto latestClockEstimate = clockEstimate;

  const auto formatDeadline = PlaybackClock::now() + 5s;
  std::optional<JitterBufferFormat> format;
  while (PlaybackClock::now() < formatDeadline) {
    receiver.rethrowIfFailed();
    format = jitterBuffer.format();
    if (format) {
      break;
    }
    std::this_thread::sleep_for(5ms);
  }
  if (!format) {
    throw std::runtime_error("Audio stream format is unavailable");
  }

  // Device initialization can take hundreds of milliseconds on Windows. Do
  // it before selecting a packet so the chosen timestamp remains in the
  // future when playback starts.
  ScheduledStreamPlayer player(jitterBuffer, *format, config_.playbackDelay);
  OutputLatency outputLatency{player.outputRouteName(), OutputRouteType::Unknown,
                              CalibrationConfidence::Manual, 0ns,
                              config_.outputLatencyAdjustment};
  const auto outputCorrection = outputLatency.effectiveLatency();

  const auto selectionDeadline = PlaybackClock::now() + 5s;
  std::optional<BufferedPacketTiming> startPacket;
  while (PlaybackClock::now() < selectionDeadline) {
    receiver.rethrowIfFailed();
    const auto clientThreshold = PlaybackClock::now() + config_.startupLead;
    const auto hostThreshold =
        clientThreshold.time_since_epoch().count() +
        static_cast<std::int64_t>(driftEstimator.offsetNanosecondsAt(
            clientThreshold.time_since_epoch().count())) -
        std::chrono::duration_cast<PlaybackClock::Duration>(
            config_.playbackDelay)
            .count() + outputCorrection.count();
    if (hostThreshold >= 0) {
      startPacket = jitterBuffer.firstPacketAtOrAfter(
          static_cast<std::uint64_t>(hostThreshold));
    }
    if (startPacket) {
      break;
    }
    std::this_thread::sleep_for(5ms);
  }
  if (!startPacket) {
    throw std::runtime_error(
        "No schedulable audio packet arrived from the host");
  }

  const auto localPresentationNanoseconds =
      static_cast<std::int64_t>(startPacket->presentationTimestampNanoseconds) -
      static_cast<std::int64_t>(driftEstimator.offsetNanosecondsAt(
          PlaybackClock::now().time_since_epoch().count())) +
      std::chrono::duration_cast<PlaybackClock::Duration>(config_.playbackDelay)
          .count() - outputCorrection.count();
  if (localPresentationNanoseconds < 0) {
    throw std::runtime_error("Translated client presentation time is invalid");
  }

  player.schedule(startPacket->startFrame,
                  PlaybackClock::Timestamp{
                      PlaybackClock::Duration{localPresentationNanoseconds}});
  player.start();

  std::uint64_t peakDepthFrames = 0;
  double bufferErrorMilliseconds = 0.0;
  auto nextClockMeasurement = PlaybackClock::now() + 5s;
  while (true) {
    std::this_thread::sleep_for(100ms);
    receiver.rethrowIfFailed();
    if (player.outputRouteChanged())
      throw std::runtime_error("Client output route changed; restart with a calibration for the new route");
    if (PlaybackClock::now() >= nextClockMeasurement) {
      try {
        latestClockEstimate = ClockSyncClient::measure(
            hostAddress, config_.clockSyncPort, 2s, 8);
        driftEstimator.addSample(
            latestClockEstimate.clientSampleTimestampNanoseconds,
            latestClockEstimate.hostMinusClientOffset.count());
      } catch (const std::runtime_error &) {
        // Keep playing with the last valid estimate during a brief outage.
      }
      nextClockMeasurement = PlaybackClock::now() + 5s;
    }
    const auto currentFrame = player.currentFrame();
    if (player.started()) {
      const auto clientNow = PlaybackClock::now().time_since_epoch().count();
      const double hostNow = static_cast<double>(clientNow) +
                             driftEstimator.offsetNanosecondsAt(clientNow);
      const double desiredFrame =
          static_cast<double>(startPacket->startFrame) +
          (hostNow - static_cast<double>(startPacket->presentationTimestampNanoseconds) -
           static_cast<double>(
               std::chrono::duration_cast<std::chrono::nanoseconds>(
                   config_.playbackDelay).count()) +
           static_cast<double>(outputCorrection.count())) *
          static_cast<double>(format->sampleRate) / 1'000'000'000.0;
      bufferErrorMilliseconds =
          (desiredFrame - static_cast<double>(currentFrame)) * 1'000.0 /
          static_cast<double>(format->sampleRate);
      player.setCorrectionRatio(driftCorrector.correctionRatio(
          driftEstimator.estimatedDriftPpm(), bufferErrorMilliseconds));
    }
    peakDepthFrames =
        std::max(peakDepthFrames, jitterBuffer.depthFrames(currentFrame));
    const auto lastPacketTime = receiver.lastPacketTime();
    if (player.started() && lastPacketTime.time_since_epoch().count() > 0 &&
        PlaybackClock::now() - lastPacketTime > 200ms &&
        currentFrame >= jitterBuffer.latestEndFrame()) {
      break;
    }
  }
  player.stop();

  DesktopAudioClientStats stats;
  stats.startingFrame = startPacket->startFrame;
  stats.finalPlaybackFrame =
      std::min(player.currentFrame(), jitterBuffer.latestEndFrame());
  stats.packetsReceived = receiver.packetsReceived();
  stats.packetsLost = receiver.packetsLost();
  stats.outOfOrderPackets = receiver.outOfOrderPackets();
  const auto bufferMetrics = jitterBuffer.metrics(player.currentFrame());
  stats.latePackets = bufferMetrics.latePackets;
  stats.duplicatePackets = bufferMetrics.duplicatePackets;
  stats.networkJitterMilliseconds = bufferMetrics.networkJitterMilliseconds;
  stats.targetBufferMilliseconds = bufferMetrics.targetBufferMilliseconds;
  const auto expectedPackets = stats.packetsReceived + stats.packetsLost;
  stats.packetLossPercent = expectedPackets == 0 ? 0.0 :
      100.0 * static_cast<double>(stats.packetsLost) /
          static_cast<double>(expectedPackets);
  const auto finalDepthFrames = jitterBuffer.depthFrames(player.currentFrame());
  stats.finalBufferDepthMilliseconds =
      1'000.0 * static_cast<double>(finalDepthFrames) / format->sampleRate;
  stats.peakBufferDepthMilliseconds =
      1'000.0 * static_cast<double>(peakDepthFrames) / format->sampleRate;
  stats.clockOffsetMilliseconds =
      driftEstimator.offsetNanosecondsAt(
          PlaybackClock::now().time_since_epoch().count()) / 1'000'000.0;
  stats.clockRoundTripMilliseconds =
      milliseconds(latestClockEstimate.roundTripTime);
  stats.clockSamples = latestClockEstimate.samples;
  stats.clockMeasurementQuality =
      stats.clockSamples >= 6 && stats.clockRoundTripMilliseconds < 10.0
          ? "Good"
          : stats.clockSamples >= 3 ? "Fair" : "Poor";
  stats.estimatedDriftPpm = driftEstimator.estimatedDriftPpm();
  stats.bufferErrorMilliseconds = bufferErrorMilliseconds;
  stats.correctionRatio = player.correctionRatio();
  stats.estimatedPlaybackDelayMilliseconds =
      milliseconds(player.estimatedPlaybackDelay());
  stats.outputLatencyAdjustmentMilliseconds = milliseconds(outputCorrection);
  stats.outputRouteName = player.outputRouteName();
  stats.underrunFrames = player.underrunFrames();
  return stats;
}
