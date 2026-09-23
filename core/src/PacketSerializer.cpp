#include "PacketSerializer.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr std::array<std::byte, 4> magic{std::byte{'S'}, std::byte{'A'},
                                         std::byte{'U'}, std::byte{'D'}};

void appendU8(std::vector<std::byte> &output, std::uint8_t value) {
  output.push_back(static_cast<std::byte>(value));
}

void appendU16(std::vector<std::byte> &output, std::uint16_t value) {
  appendU8(output, static_cast<std::uint8_t>(value >> 8U));
  appendU8(output, static_cast<std::uint8_t>(value));
}

void appendU32(std::vector<std::byte> &output, std::uint32_t value) {
  appendU16(output, static_cast<std::uint16_t>(value >> 16U));
  appendU16(output, static_cast<std::uint16_t>(value));
}

void appendU64(std::vector<std::byte> &output, std::uint64_t value) {
  appendU32(output, static_cast<std::uint32_t>(value >> 32U));
  appendU32(output, static_cast<std::uint32_t>(value));
}

class Reader {
public:
  explicit Reader(std::span<const std::byte> data) : data_(data) {}

  std::uint8_t readU8() {
    requireAvailable(1);
    return std::to_integer<std::uint8_t>(data_[offset_++]);
  }

  std::uint16_t readU16() {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(readU8()) << 8U) | readU8());
  }

  std::uint32_t readU32() {
    return (static_cast<std::uint32_t>(readU16()) << 16U) | readU16();
  }

  std::uint64_t readU64() {
    return (static_cast<std::uint64_t>(readU32()) << 32U) | readU32();
  }

  std::span<const std::byte> readBytes(std::size_t count) {
    requireAvailable(count);
    const auto result = data_.subspan(offset_, count);
    offset_ += count;
    return result;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return data_.size() - offset_;
  }

private:
  void requireAvailable(std::size_t count) const {
    if (count > remaining()) {
      throw std::invalid_argument("Audio packet is truncated");
    }
  }

  std::span<const std::byte> data_;
  std::size_t offset_{};
};

std::size_t bytesPerSample(AudioSampleFormat format) {
  if (format != AudioSampleFormat::PcmS16LittleEndian) {
    throw std::invalid_argument("Unsupported audio sample format");
  }
  return 2;
}

void validate(const AudioPacket &packet) {
  if (packet.protocolVersion != AudioPacket::CurrentProtocolVersion) {
    throw std::invalid_argument("Unsupported audio protocol version");
  }
  if (packet.sampleRate < 8'000 || packet.sampleRate > 192'000) {
    throw std::invalid_argument("Audio packet sample rate is unsupported");
  }
  if (packet.channelCount == 0 || packet.channelCount > 2) {
    throw std::invalid_argument("Audio packet channel count is unsupported");
  }
  if (packet.streamId == 0 || packet.frameCount == 0 ||
      packet.startFrame > std::numeric_limits<std::uint64_t>::max() -
                              packet.frameCount ||
      packet.presentationTimestampNanoseconds >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    throw std::invalid_argument("Audio packet identifiers or timing are invalid");
  }
  if (packet.pcmPayload.empty()) {
    throw std::invalid_argument("Audio packet PCM payload must not be empty");
  }
  if (packet.pcmPayload.size() > PacketSerializer::MaximumPayloadSize) {
    throw std::invalid_argument("Audio packet exceeds the payload size limit");
  }

  const auto bytesPerFrame =
      bytesPerSample(packet.sampleFormat) * packet.channelCount;
  if (packet.pcmPayload.size() % bytesPerFrame != 0) {
    throw std::invalid_argument("PCM payload does not contain complete frames");
  }
  const auto calculatedFrameCount = packet.pcmPayload.size() / bytesPerFrame;
  if (calculatedFrameCount != packet.frameCount) {
    throw std::invalid_argument("PCM payload size does not match frame count");
  }
}

} // namespace

std::vector<std::byte> PacketSerializer::serialize(const AudioPacket &packet) {
  validate(packet);
  if (packet.pcmPayload.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::invalid_argument(
        "PCM payload cannot be represented on the wire");
  }

  std::vector<std::byte> output;
  output.reserve(HeaderSize + packet.pcmPayload.size());
  output.insert(output.end(), magic.begin(), magic.end());
  appendU8(output, packet.protocolVersion);
  appendU8(output, static_cast<std::uint8_t>(HeaderSize));
  appendU16(output, packet.flags);
  appendU64(output, packet.sessionId);
  appendU32(output, packet.sequenceNumber);
  appendU32(output, packet.sampleRate);
  appendU16(output, packet.channelCount);
  appendU8(output, static_cast<std::uint8_t>(packet.sampleFormat));
  appendU8(output, 0); // Reserved.
  appendU64(output, packet.startFrame);
  appendU64(output, packet.presentationTimestampNanoseconds);
  appendU16(output, static_cast<std::uint16_t>(packet.pcmPayload.size()));
  appendU16(output, packet.frameCount);
  appendU32(output, packet.streamId);
  output.insert(output.end(), packet.pcmPayload.begin(),
                packet.pcmPayload.end());
  return output;
}

AudioPacket PacketSerializer::deserialize(std::span<const std::byte> data) {
  if (data.size() < HeaderSize) {
    throw std::invalid_argument("Audio packet is shorter than the header");
  }
  if (data.size() > MaximumDatagramSize) {
    throw std::invalid_argument("Audio datagram exceeds the MTU-safe limit");
  }

  Reader reader(data);
  const auto receivedMagic = reader.readBytes(magic.size());
  if (!std::equal(receivedMagic.begin(), receivedMagic.end(), magic.begin())) {
    throw std::invalid_argument("Audio packet magic is invalid");
  }

  AudioPacket packet;
  packet.protocolVersion = reader.readU8();
  const auto headerSize = reader.readU8();
  if (headerSize != HeaderSize) {
    throw std::invalid_argument("Audio packet header size is unsupported");
  }
  packet.flags = reader.readU16();
  packet.sessionId = reader.readU64();
  packet.sequenceNumber = reader.readU32();
  packet.sampleRate = reader.readU32();
  packet.channelCount = reader.readU16();
  packet.sampleFormat = static_cast<AudioSampleFormat>(reader.readU8());
  static_cast<void>(reader.readU8()); // Reserved.
  packet.startFrame = reader.readU64();
  packet.presentationTimestampNanoseconds = reader.readU64();
  const auto payloadSize = reader.readU16();
  packet.frameCount = reader.readU16();
  packet.streamId = reader.readU32();

  if (reader.remaining() != payloadSize) {
    throw std::invalid_argument("Audio packet payload size is inconsistent");
  }
  const auto payload = reader.readBytes(payloadSize);
  packet.pcmPayload.assign(payload.begin(), payload.end());
  validate(packet);
  return packet;
}
