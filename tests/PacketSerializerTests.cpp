#include "AudioPacket.hpp"
#include "PacketSerializer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

template <typename Function>
void requireInvalid(Function function, std::string_view message) {
  try {
    function();
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error(std::string(message));
}

AudioPacket makePacket() {
  AudioPacket packet;
  packet.flags = 0x1234;
  packet.sessionId = 0x0102030405060708ULL;
  packet.streamId = 0x01020304U;
  packet.sequenceNumber = 0x11223344U;
  packet.sampleRate = 48'000;
  packet.channelCount = 2;
  packet.startFrame = 0x1011121314151617ULL;
  packet.presentationTimestampNanoseconds = 0x2021222324252627ULL;
  packet.frameCount = 4;
  packet.pcmPayload.resize(16);
  for (std::size_t index = 0; index < packet.pcmPayload.size(); ++index) {
    packet.pcmPayload[index] = static_cast<std::byte>(index);
  }
  return packet;
}

void testRoundTripAndByteOrder() {
  const auto packet = makePacket();
  const auto encoded = PacketSerializer::serialize(packet);
  require(encoded.size() == PacketSerializer::HeaderSize + 16,
          "Encoded packet size is incorrect");
  require(encoded[0] == std::byte{'S'} && encoded[1] == std::byte{'A'} &&
              encoded[2] == std::byte{'U'} && encoded[3] == std::byte{'D'},
          "Packet magic is incorrect");
  require(encoded[4] == std::byte{2} && encoded[5] == std::byte{52},
          "Version or header size is incorrect");
  require(encoded[6] == std::byte{0x12} && encoded[7] == std::byte{0x34},
          "16-bit field is not big-endian");
  require(encoded[8] == std::byte{0x01} && encoded[15] == std::byte{0x08},
          "64-bit field is not big-endian");
  require(encoded[16] == std::byte{0x11} && encoded[19] == std::byte{0x44},
          "32-bit field is not big-endian");
  require(encoded[48] == std::byte{0x01} && encoded[51] == std::byte{0x04},
          "Stream ID is not big-endian");

  const auto decoded = PacketSerializer::deserialize(encoded);
  require(decoded.protocolVersion == packet.protocolVersion,
          "Protocol version did not round-trip");
  require(decoded.flags == packet.flags, "Flags did not round-trip");
  require(decoded.sessionId == packet.sessionId,
          "Session ID did not round-trip");
  require(decoded.streamId == packet.streamId,
          "Stream ID did not round-trip");
  require(decoded.sequenceNumber == packet.sequenceNumber,
          "Sequence number did not round-trip");
  require(decoded.sampleRate == packet.sampleRate,
          "Sample rate did not round-trip");
  require(decoded.channelCount == packet.channelCount,
          "Channel count did not round-trip");
  require(decoded.startFrame == packet.startFrame,
          "Start frame did not round-trip");
  require(decoded.presentationTimestampNanoseconds ==
              packet.presentationTimestampNanoseconds,
          "Presentation timestamp did not round-trip");
  require(decoded.frameCount == packet.frameCount,
          "Frame count did not round-trip");
  require(decoded.pcmPayload == packet.pcmPayload,
          "PCM payload did not round-trip");
}

void testMaximumPacket() {
  auto packet = makePacket();
  packet.frameCount = 287;
  packet.pcmPayload.assign(PacketSerializer::MaximumPayloadSize, std::byte{0});
  const auto encoded = PacketSerializer::serialize(packet);
  require(encoded.size() == PacketSerializer::MaximumDatagramSize,
          "Maximum packet does not match the datagram limit");
  require(PacketSerializer::deserialize(encoded).frameCount == 287,
          "Maximum packet failed to decode");
}

void testInvalidPackets() {
  const auto valid = PacketSerializer::serialize(makePacket());

  auto truncated = valid;
  truncated.pop_back();
  requireInvalid(
      [&] { static_cast<void>(PacketSerializer::deserialize(truncated)); },
      "A truncated payload must be rejected");

  auto badMagic = valid;
  badMagic[0] = std::byte{'X'};
  requireInvalid(
      [&] { static_cast<void>(PacketSerializer::deserialize(badMagic)); },
      "Invalid magic must be rejected");

  auto badVersion = makePacket();
  badVersion.protocolVersion = 3;
  requireInvalid(
      [&] { static_cast<void>(PacketSerializer::serialize(badVersion)); },
      "An unsupported protocol version must be rejected");

  auto badFrameCount = makePacket();
  badFrameCount.frameCount = 3;
  requireInvalid(
      [&] { static_cast<void>(PacketSerializer::serialize(badFrameCount)); },
      "A mismatched frame count must be rejected");

  auto oversized = makePacket();
  oversized.frameCount = 289;
  oversized.pcmPayload.assign(PacketSerializer::MaximumPayloadSize + 4,
                              std::byte{0});
  requireInvalid(
      [&] { static_cast<void>(PacketSerializer::serialize(oversized)); },
      "An oversized payload must be rejected");
}

} // namespace

int main() {
  try {
    testRoundTripAndByteOrder();
    testMaximumPacket();
    testInvalidPackets();
    std::cout << "PacketSerializer tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
