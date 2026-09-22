#include "JitterBuffer.hpp"

#include <algorithm>
#include <cstddef>
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
      packet.sampleRate == 0 || packet.channelCount == 0 ||
      packet.frameCount == 0) {
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
    return false;
  }
  latestEndFrame_ =
      std::max(latestEndFrame_, packet.startFrame + packet.frameCount);
  return true;
}

std::uint32_t
JitterBuffer::readFrames(std::uint64_t startFrame,
                         std::span<std::int16_t> interleavedOutput,
                         bool retainLookahead) {
  std::scoped_lock lock(mutex_);
  if (!format_) {
    std::fill(interleavedOutput.begin(), interleavedOutput.end(),
              std::int16_t{0});
    return 0;
  }
  const auto channels = format_->channelCount;
  if (interleavedOutput.size() % channels != 0) {
    throw std::invalid_argument("Jitter buffer output is not frame aligned");
  }

  std::fill(interleavedOutput.begin(), interleavedOutput.end(),
            std::int16_t{0});
  const auto requestedFrames = interleavedOutput.size() / channels;
  const auto requestedEnd = startFrame + requestedFrames;
  std::uint32_t copiedFrames = 0;

  for (auto iterator = packets_.begin(); iterator != packets_.end();) {
    const auto packetStart = iterator->second.startFrame;
    const auto packetEnd = packetStart + iterator->second.frameCount;
    if (packetEnd <= startFrame) {
      iterator = packets_.erase(iterator);
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

    if (!retainLookahead && packetEnd <= requestedEnd) {
      iterator = packets_.erase(iterator);
    } else {
      // A rate-adjusted reader may need the last sample again at a boundary.
      ++iterator;
    }
  }
  return copiedFrames;
}

std::optional<BufferedPacketTiming> JitterBuffer::firstPacketAtOrAfter(
    std::uint64_t hostTimestampNanoseconds) const {
  std::scoped_lock lock(mutex_);
  for (const auto &[startFrame, packet] : packets_) {
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
  std::scoped_lock lock(mutex_);
  return latestEndFrame_;
}

std::uint64_t JitterBuffer::depthFrames(std::uint64_t playbackFrame) const {
  std::scoped_lock lock(mutex_);
  return latestEndFrame_ > playbackFrame ? latestEndFrame_ - playbackFrame : 0;
}

std::size_t JitterBuffer::packetCount() const {
  std::scoped_lock lock(mutex_);
  return packets_.size();
}
