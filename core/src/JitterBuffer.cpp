#include "JitterBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

std::vector<std::int16_t> decodePcm(const AudioPacket &packet) {
  std::vector<std::int16_t> samples(packet.pcmPayload.size() / 2U);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto low =
        std::to_integer<std::uint16_t>(packet.pcmPayload[index * 2U]);
    const auto high =
        std::to_integer<std::uint16_t>(packet.pcmPayload[index * 2U + 1U]);
    samples[index] = static_cast<std::int16_t>(low | (high << 8U));
  }
  return samples;
}

} // namespace

bool JitterBuffer::push(const AudioPacket &packet) {
  if (packet.sampleFormat != AudioSampleFormat::PcmS16LittleEndian ||
      packet.sampleRate < 8'000 || packet.sampleRate > 192'000 ||
      packet.channelCount == 0 || packet.channelCount > 2 ||
      packet.frameCount == 0 ||
      packet.startFrame > std::numeric_limits<std::uint64_t>::max() -
                              packet.frameCount) {
    throw std::invalid_argument(
        "Jitter buffer received an invalid audio format");
  }
  const auto expectedBytes = static_cast<std::size_t>(packet.frameCount) *
                             packet.channelCount * sizeof(std::int16_t);
  if (packet.pcmPayload.size() != expectedBytes) {
    throw std::invalid_argument("Jitter buffer PCM size does not match frames");
  }

  BufferedPacket buffered;
  buffered.startFrame = packet.startFrame;
  buffered.presentationTimestampNanoseconds =
      packet.presentationTimestampNanoseconds;
  buffered.frameCount = packet.frameCount;
  buffered.samples = decodePcm(packet);

  std::scoped_lock lock(mutex_);
  for (auto iterator = packets_.begin(); iterator != packets_.end();) {
    if (iterator->second.startFrame + iterator->second.frameCount <=
        latestReadFrame_)
      iterator = packets_.erase(iterator);
    else
      break;
  }
  if (packet.startFrame + packet.frameCount <= latestReadFrame_) {
    ++metrics_.latePackets;
    return false;
  }
  const JitterBufferFormat incomingFormat{packet.sampleRate,
                                          packet.channelCount};
  if (format_ && (format_->sampleRate != incomingFormat.sampleRate ||
                  format_->channelCount != incomingFormat.channelCount)) {
    throw std::invalid_argument("Audio stream format changed within a session");
  }
  format_ = incomingFormat;
  const auto [iterator, inserted] =
      packets_.try_emplace(packet.startFrame, std::move(buffered));
  if (!inserted) {
    ++metrics_.duplicatePackets;
    return false;
  }
  const auto arrival = std::chrono::steady_clock::now();
  if (lastPresentationNanoseconds_ != 0 &&
      packet.presentationTimestampNanoseconds > lastPresentationNanoseconds_) {
    const auto arrivalGap =
        std::chrono::duration<double, std::milli>(arrival - lastArrival_).count();
    const auto streamGap =
        static_cast<double>(packet.presentationTimestampNanoseconds -
                            lastPresentationNanoseconds_) / 1'000'000.0;
    const auto deviation = std::abs(arrivalGap - streamGap);
    metrics_.networkJitterMilliseconds +=
        (deviation - metrics_.networkJitterMilliseconds) / 16.0;
    const auto desired = std::clamp(150.0 + 4.0 * metrics_.networkJitterMilliseconds,
                                    120.0, 350.0);
    if (desired > metrics_.targetBufferMilliseconds) {
      metrics_.targetBufferMilliseconds =
          std::min(desired, metrics_.targetBufferMilliseconds + 10.0);
    } else {
      metrics_.targetBufferMilliseconds =
          std::max(desired, metrics_.targetBufferMilliseconds - 0.25);
    }
  }
  lastArrival_ = arrival;
  lastPresentationNanoseconds_ = packet.presentationTimestampNanoseconds;
  latestEndFrame_ =
      std::max(latestEndFrame_, packet.startFrame + packet.frameCount);
  latestEndFrameAtomic_.store(latestEndFrame_, std::memory_order_release);
  if (packets_.size() > 1'024) {
    packets_.erase(packets_.begin());
    ++metrics_.overflowPackets;
  }
  return true;
}

std::uint32_t
JitterBuffer::readFrames(std::uint64_t startFrame,
                         std::span<std::int16_t> interleavedOutput,
                         bool retainLookahead) {
  std::fill(interleavedOutput.begin(), interleavedOutput.end(),
            std::int16_t{0});
  std::unique_lock lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) return 0;
  if (!format_) {
    return 0;
  }
  const auto channels = format_->channelCount;
  if (interleavedOutput.size() % channels != 0) {
    throw std::invalid_argument("Jitter buffer output is not frame aligned");
  }

  const auto requestedFrames = interleavedOutput.size() / channels;
  const auto requestedEnd = startFrame + requestedFrames;
  latestReadFrame_ = std::max(
      latestReadFrame_, requestedEnd - (retainLookahead ? std::min<std::size_t>(2, requestedFrames) : 0));
  std::uint32_t copiedFrames = 0;

  for (auto iterator = packets_.begin(); iterator != packets_.end(); ++iterator) {
    const auto packetStart = iterator->second.startFrame;
    const auto packetEnd = packetStart + iterator->second.frameCount;
    if (packetEnd <= startFrame) {
      continue;
    }
    if (packetStart >= requestedEnd) {
      break;
    }

    const auto copyStart = std::max(startFrame, packetStart);
    const auto copyEnd = std::min<std::uint64_t>(requestedEnd, packetEnd);
    if (copyStart < copyEnd) {
      const auto frames = copyEnd - copyStart;
      const auto sourceOffset =
          static_cast<std::size_t>(copyStart - packetStart) * channels;
      const auto destinationOffset =
          static_cast<std::size_t>(copyStart - startFrame) * channels;
      std::copy_n(iterator->second.samples.begin() + sourceOffset,
                  static_cast<std::size_t>(frames) * channels,
                  interleavedOutput.begin() + destinationOffset);
      copiedFrames += static_cast<std::uint32_t>(frames);
    }

    // The receiver thread removes expired packets on its next push. The
    // callback never frees map nodes or waits for a mutex.
  }
  return copiedFrames;
}

std::optional<BufferedPacketTiming> JitterBuffer::firstPacketAtOrAfter(
    std::uint64_t hostTimestampNanoseconds) const {
  std::scoped_lock lock(mutex_);
  for (const auto &[startFrame, packet] : packets_) {
    if (packet.startFrame + packet.frameCount <= latestReadFrame_) continue;
    if (packet.presentationTimestampNanoseconds >= hostTimestampNanoseconds) {
      return BufferedPacketTiming{startFrame,
                                  packet.presentationTimestampNanoseconds};
    }
  }
  return std::nullopt;
}

std::optional<JitterBufferFormat> JitterBuffer::format() const {
  std::scoped_lock lock(mutex_);
  return format_;
}

std::uint64_t JitterBuffer::latestEndFrame() const {
  return latestEndFrameAtomic_.load(std::memory_order_acquire);
}

std::uint64_t JitterBuffer::depthFrames(std::uint64_t playbackFrame) const {
  std::scoped_lock lock(mutex_);
  return latestEndFrame_ > playbackFrame ? latestEndFrame_ - playbackFrame : 0;
}

std::size_t JitterBuffer::packetCount() const {
  std::scoped_lock lock(mutex_);
  return static_cast<std::size_t>(std::count_if(
      packets_.begin(), packets_.end(), [this](const auto &entry) {
        return entry.second.startFrame + entry.second.frameCount >
               latestReadFrame_;
      }));
}

JitterBufferMetrics JitterBuffer::metrics(std::uint64_t playbackFrame) const {
  std::scoped_lock lock(mutex_);
  auto result = metrics_;
  if (format_ && latestEndFrame_ > playbackFrame) {
    result.actualBufferMilliseconds =
        1'000.0 * static_cast<double>(latestEndFrame_ - playbackFrame) /
        static_cast<double>(format_->sampleRate);
  }
  return result;
}
