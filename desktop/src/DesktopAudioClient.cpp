#include "DesktopAudioClient.hpp"

#include "ClockSync.hpp"
#include "JitterBuffer.hpp"
#include "PacketSerializer.hpp"
#include "PlaybackClock.hpp"
#include "UdpAudioReceiver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
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
        } else if (packet.sessionId != *sessionId_) {
          continue;
        }
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
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = format.channelCount;
    config.sampleRate = format.sampleRate;
    config.periodSizeInMilliseconds = 10;
    config.dataCallback = &ScheduledStreamPlayer::dataCallback;
    config.pUserData = this;
    const auto result = ma_device_init(nullptr, &config, &device_);
    if (result != MA_SUCCESS) {
      throw std::runtime_error(
          std::string("Unable to initialize client audio: ") +
          ma_result_description(result));
    }
    initialized_ = true;
  }

  ~ScheduledStreamPlayer() { stop(); }

  void schedule(std::uint64_t startFrame, PlaybackClock::Timestamp startTime) {
    currentFrame_.store(startFrame, std::memory_order_release);
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

private:
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

    const auto currentFrame =
        self->currentFrame_.load(std::memory_order_relaxed);
    const auto copiedFrames =
        self->jitterBuffer_.readFrames(currentFrame, samples);
    const auto latestEndFrame = self->jitterBuffer_.latestEndFrame();
    const auto knownFrames =
        latestEndFrame > currentFrame
            ? std::min<std::uint64_t>(frameCount, latestEndFrame - currentFrame)
            : 0;
    if (copiedFrames < knownFrames) {
      self->underrunFrames_.fetch_add(knownFrames - copiedFrames,
                                      std::memory_order_release);
    }
    self->currentFrame_.store(currentFrame + frameCount,
                              std::memory_order_release);
  }

  JitterBuffer &jitterBuffer_;
  std::uint16_t channelCount_{};
  std::atomic<std::uint64_t> currentFrame_{};
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
      config_.playbackDelay < 0ms || config_.startupLead <= 0ms) {
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

  const auto selectionDeadline = PlaybackClock::now() + 5s;
  std::optional<BufferedPacketTiming> startPacket;
  while (PlaybackClock::now() < selectionDeadline) {
    receiver.rethrowIfFailed();
    const auto clientThreshold = PlaybackClock::now() + config_.startupLead;
    const auto hostThreshold =
        clientThreshold.time_since_epoch().count() +
        clockEstimate.hostMinusClientOffset.count() -
        std::chrono::duration_cast<PlaybackClock::Duration>(
            config_.playbackDelay)
            .count();
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
      clockEstimate.hostMinusClientOffset.count() +
      std::chrono::duration_cast<PlaybackClock::Duration>(config_.playbackDelay)
          .count();
  if (localPresentationNanoseconds < 0) {
    throw std::runtime_error("Translated client presentation time is invalid");
  }

  player.schedule(startPacket->startFrame,
                  PlaybackClock::Timestamp{
                      PlaybackClock::Duration{localPresentationNanoseconds}});
  player.start();

  std::uint64_t peakDepthFrames = 0;
  while (true) {
    std::this_thread::sleep_for(100ms);
    receiver.rethrowIfFailed();
    const auto currentFrame = player.currentFrame();
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
  const auto finalDepthFrames = jitterBuffer.depthFrames(player.currentFrame());
  stats.finalBufferDepthMilliseconds =
      1'000.0 * static_cast<double>(finalDepthFrames) / format->sampleRate;
  stats.peakBufferDepthMilliseconds =
      1'000.0 * static_cast<double>(peakDepthFrames) / format->sampleRate;
  stats.clockOffsetMilliseconds =
      milliseconds(clockEstimate.hostMinusClientOffset);
  stats.clockRoundTripMilliseconds = milliseconds(clockEstimate.roundTripTime);
  stats.estimatedPlaybackDelayMilliseconds =
      milliseconds(player.estimatedPlaybackDelay());
  stats.underrunFrames = player.underrunFrames();
  return stats;
}
