#include "DesktopNetworkHost.hpp"

#include "AudioPacket.hpp"
#include "PacketSerializer.hpp"
#include "Room.hpp"
#include "UdpAudioSender.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <miniaudio.h>

namespace {

class Decoder {
public:
  explicit Decoder(const std::filesystem::path &path) {
    const auto config = ma_decoder_config_init(ma_format_s16, 0, 0);
    const auto result =
        ma_decoder_init_file(path.string().c_str(), &config, &decoder_);
    if (result != MA_SUCCESS) {
      throw std::runtime_error(
          std::string("Unable to open network audio source: ") +
          ma_result_description(result));
    }
    initialized_ = true;
  }

  ~Decoder() {
    if (initialized_) {
      ma_decoder_uninit(&decoder_);
    }
  }

  Decoder(const Decoder &) = delete;
  Decoder &operator=(const Decoder &) = delete;

  [[nodiscard]] std::uint32_t sampleRate() const noexcept {
    return decoder_.outputSampleRate;
  }

  [[nodiscard]] std::uint16_t channels() const {
    if (decoder_.outputChannels > std::numeric_limits<std::uint16_t>::max()) {
      throw std::runtime_error("Audio channel count is unsupported");
    }
    return static_cast<std::uint16_t>(decoder_.outputChannels);
  }

  std::uint64_t read(std::span<std::int16_t> samples,
                     std::uint64_t requestedFrames) {
    ma_uint64 framesRead = 0;
    const auto result = ma_decoder_read_pcm_frames(
        &decoder_, samples.data(), requestedFrames, &framesRead);
    if (result != MA_SUCCESS && result != MA_AT_END) {
      throw std::runtime_error(std::string("Unable to decode network audio: ") +
                               ma_result_description(result));
    }
    return framesRead;
  }

  void seek(std::uint64_t frame) {
    const auto result = ma_decoder_seek_to_pcm_frame(&decoder_, frame);
    if (result != MA_SUCCESS)
      throw std::runtime_error("Unable to seek network audio source");
  }

private:
  ma_decoder decoder_{};
  bool initialized_{false};
};

std::uint64_t createSessionId() {
  std::random_device random;
  const auto high = static_cast<std::uint64_t>(random()) << 32U;
  const auto low = static_cast<std::uint64_t>(random());
  return high | low;
}

std::vector<std::byte>
toLittleEndianPcm(std::span<const std::int16_t> samples) {
  std::vector<std::byte> result;
  result.reserve(samples.size() * sizeof(std::int16_t));
  for (const auto sample : samples) {
    const auto bits = static_cast<std::uint16_t>(sample);
    result.push_back(static_cast<std::byte>(bits & 0xFFU));
    result.push_back(static_cast<std::byte>(bits >> 8U));
  }
  return result;
}

} // namespace

DesktopNetworkHost::DesktopNetworkHost(DesktopNetworkHostConfig config)
    : config_(std::move(config)) {
  if (config_.port == 0 || config_.streamId == 0) {
    throw std::invalid_argument("Host UDP port must be non-zero");
  }
  if (config_.desiredPacketDuration <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("Packet duration must be positive");
  }
  if (config_.sendAhead < std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("Send-ahead duration must not be negative");
  }

  if (config_.hostOutputLatency < std::chrono::milliseconds{-1'000} ||
      config_.hostOutputLatency > std::chrono::milliseconds{1'000})
    throw std::invalid_argument("Host output latency calibration is invalid");
}

DesktopNetworkHostStats
DesktopNetworkHost::streamFile(const std::filesystem::path &path,
                               PlaybackClock::Timestamp playbackStartTime,
                               std::uint32_t expectedSampleRate,
                               std::uint16_t expectedChannelCount,
                               std::uint64_t initialFrame) {
  Decoder decoder(path);
  if (initialFrame != 0) decoder.seek(initialFrame);
  const auto sampleRate = decoder.sampleRate();
  const auto channelCount = decoder.channels();
  if (sampleRate != expectedSampleRate ||
      channelCount != expectedChannelCount) {
    throw std::runtime_error(
        "Network decoder format does not match local playback format");
  }

  const auto bytesPerFrame =
      static_cast<std::size_t>(channelCount) * sizeof(std::int16_t);
  const auto maximumFrames =
      PacketSerializer::MaximumPayloadSize / bytesPerFrame;
  const auto desiredFrames = std::max<std::uint64_t>(
      1,
      (static_cast<std::uint64_t>(sampleRate) *
           static_cast<std::uint64_t>(config_.desiredPacketDuration.count()) +
       999U) /
          1'000U);
  const auto framesPerPacket =
      std::min<std::uint64_t>(desiredFrames, maximumFrames);
  if (framesPerPacket == 0 ||
      framesPerPacket > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("Audio format cannot fit in an MTU-safe packet");
  }

  DesktopNetworkHostStats stats;
  stats.sessionId = config_.sessionId != 0 ? config_.sessionId : createSessionId();
  stats.framesPerPacket = static_cast<std::uint16_t>(framesPerPacket);
  stats.packetDurationMilliseconds = 1'000.0 *
                                     static_cast<double>(framesPerPacket) /
                                     static_cast<double>(sampleRate);

  UdpAudioSender sender(config_.destinationAddress, config_.port);
  const auto deliver = [&](std::span<const std::byte> bytes) {
    if (config_.room) {
      for (const auto &member : config_.room->snapshot().members) {
        try {
          sender.sendTo(bytes, member.ipv4Address, member.audioPort);
          stats.audioDatagramBytesSent += bytes.size();
          ++stats.datagramsSent;
        } catch (const std::exception &) {
          ++stats.clientSendFailures;
        }
      }
    } else {
      sender.send(bytes);
      stats.audioDatagramBytesSent += bytes.size();
      ++stats.datagramsSent;
    }
  };
#ifndef NDEBUG
  std::optional<NetworkImpairment> impairment;
  if (config_.impairment) impairment.emplace(*config_.impairment);
  const auto impairmentNow = [] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
  };
  const auto dispatchReady = [&] {
    for (const auto &ready : impairment->drain(impairmentNow())) deliver(ready.bytes);
  };
#endif
  // SAUD timestamps now refer to estimated acoustic presentation, including
  // the host's local output path. The host never delays its audio callback.
  PlaybackClock clock(sampleRate, playbackStartTime + config_.hostOutputLatency);
  std::vector<std::int16_t> samples(framesPerPacket * channelCount);
  std::uint64_t startFrame = initialFrame;
  std::uint32_t sequenceNumber = 0;
  auto firstSend = std::chrono::steady_clock::time_point{};
  auto lastSend = firstSend;

  while (true) {
    if (config_.outputRouteChanged && config_.outputRouteChanged())
      throw std::runtime_error("Host output route changed; restart with a calibration for the new route");
    const auto presentationTimestamp = clock.frameToTimestamp(startFrame - initialFrame);
    auto requestedFrames = framesPerPacket;
    if (config_.room) {
      const auto pending = config_.room->snapshot().pendingAction;
      if (pending) {
        const auto cutoff = PlaybackClock::Timestamp{
            PlaybackClock::Duration{pending->effectiveHostNanoseconds}} -
            (pending->action == RoomAction::Seek ? config_.sendAhead
                                                 : std::chrono::milliseconds{0});
        if (presentationTimestamp >= cutoff) {
          stats.endedForCommand = true;
          break;
        }
        const auto framesUntilCutoff = std::chrono::duration<double>(
            cutoff - presentationTimestamp).count() * sampleRate;
        if (framesUntilCutoff < static_cast<double>(requestedFrames)) {
          requestedFrames = std::max<std::uint64_t>(
              1, static_cast<std::uint64_t>(framesUntilCutoff));
        }
      }
    }
    const auto framesRead = decoder.read(samples, requestedFrames);
    if (framesRead == 0) {
      stats.sourceExhausted = true;
      break;
    }
    const auto sendTime = presentationTimestamp - config_.sendAhead;
    if (const auto currentTime = PlaybackClock::now(); sendTime > currentTime) {
      std::this_thread::sleep_until(sendTime);
    }

    const auto sampleCount =
        static_cast<std::size_t>(framesRead) * channelCount;
    AudioPacket packet;
    packet.sessionId = stats.sessionId;
    packet.streamId = config_.streamId;
    packet.sequenceNumber = sequenceNumber++;
    packet.sampleRate = sampleRate;
    packet.channelCount = channelCount;
    packet.startFrame = startFrame;
    const auto timestampCount =
        presentationTimestamp.time_since_epoch().count();
    if (timestampCount < 0) {
      throw std::runtime_error("Host monotonic timestamp is negative");
    }
    packet.presentationTimestampNanoseconds =
        static_cast<std::uint64_t>(timestampCount);
    packet.frameCount = static_cast<std::uint16_t>(framesRead);
    packet.pcmPayload = toLittleEndianPcm(
        std::span<const std::int16_t>(samples.data(), sampleCount));

    const auto datagram = PacketSerializer::serialize(packet);
    if (stats.packetsSent == 0) firstSend = std::chrono::steady_clock::now();
#ifndef NDEBUG
    if (impairment) {
      impairment->submit(datagram, impairmentNow());
      dispatchReady();
    } else
#endif
      deliver(datagram);
    lastSend = std::chrono::steady_clock::now();
    ++stats.packetsSent;
    stats.framesSent += framesRead;
    startFrame += framesRead;
    if (config_.progressFrame) {
      config_.progressFrame->store(startFrame, std::memory_order_release);
    }
  }
  stats.lastFrame = startFrame;

#ifndef NDEBUG
  if (impairment) {
    while (impairment->pending() != 0) {
      dispatchReady();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
#endif

  if (stats.packetsSent > 1) {
    stats.sendDurationSeconds =
        std::chrono::duration<double>(lastSend - firstSend).count();
  }

  return stats;
}

const DesktopNetworkHostConfig &DesktopNetworkHost::config() const noexcept {
  return config_;
}
