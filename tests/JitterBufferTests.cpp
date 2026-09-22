#include "JitterBuffer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

AudioPacket makePacket(std::uint64_t startFrame, std::uint32_t sequence,
                       std::int16_t firstSample) {
  AudioPacket packet;
  packet.sessionId = 7;
  packet.sequenceNumber = sequence;
  packet.sampleRate = 48'000;
  packet.channelCount = 2;
  packet.startFrame = startFrame;
  packet.presentationTimestampNanoseconds = 1'000'000 + startFrame * 20'834;
  packet.frameCount = 4;
  for (std::int16_t sample = firstSample; sample < firstSample + 8; ++sample) {
    const auto bits = static_cast<std::uint16_t>(sample);
    packet.pcmPayload.push_back(static_cast<std::byte>(bits & 0xFFU));
    packet.pcmPayload.push_back(static_cast<std::byte>(bits >> 8U));
  }
  return packet;
}

void testReorderingGapAndRead() {
  JitterBuffer buffer;
  const auto later = makePacket(8, 2, 101);
  const auto earlier = makePacket(0, 0, 1);

  require(buffer.push(later), "Later packet should be inserted");
  require(buffer.push(earlier),
          "Earlier packet should be inserted out of order");
  require(!buffer.push(earlier), "Duplicate starting frame must be rejected");
  require(buffer.packetCount() == 2, "Unexpected buffered packet count");
  require(buffer.latestEndFrame() == 12, "Latest frame is incorrect");
  require(buffer.depthFrames(2) == 10, "Buffer depth is incorrect");

  const auto format = buffer.format();
  require(format && format->sampleRate == 48'000 && format->channelCount == 2,
          "Buffered format is incorrect");
  const auto timing = buffer.firstPacketAtOrAfter(1'000'001);
  require(timing && timing->startFrame == 8,
          "Timestamp-based packet selection failed");

  std::array<std::int16_t, 24> output{};
  const auto copied = buffer.readFrames(0, output);
  require(copied == 8, "Unexpected number of copied frames");
  for (std::size_t index = 0; index < 8; ++index) {
    require(output[index] == static_cast<std::int16_t>(index + 1),
            "First packet samples are incorrect");
  }
  for (std::size_t index = 8; index < 16; ++index) {
    require(output[index] == 0, "Packet gap must remain silent");
  }
  for (std::size_t index = 16; index < 24; ++index) {
    require(output[index] == static_cast<std::int16_t>(index - 15 + 100),
            "Out-of-order packet samples are incorrect");
  }
  require(buffer.packetCount() == 0, "Consumed packets must be removed");
}

void testFormatValidation() {
  JitterBuffer buffer;
  auto packet = makePacket(0, 0, 1);
  require(buffer.push(packet), "Initial packet should be inserted");
  packet.startFrame = 4;
  packet.channelCount = 1;

  bool rejected = false;
  try {
    static_cast<void>(buffer.push(packet));
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "A mid-stream format change must be rejected");
}

void testInterpolationLookahead() {
  JitterBuffer buffer;
  require(buffer.push(makePacket(0, 0, 1)), "Packet should be inserted");
  std::array<std::int16_t, 8> first{};
  std::array<std::int16_t, 4> overlap{};
  require(buffer.readFrames(0, first, true) == 4,
          "First rate-adjusted read failed");
  require(buffer.packetCount() == 1,
          "Lookahead sample must remain available");
  require(buffer.readFrames(3, overlap, true) == 1 && overlap[0] == 7,
          "Overlapping interpolation read lost its final sample");
  static_cast<void>(buffer.readFrames(4, overlap, true));
  require(buffer.packetCount() == 0,
          "Packet should be discarded after the cursor passes it");
}

} // namespace

int main() {
  try {
    testReorderingGapAndRead();
    testFormatValidation();
    testInterpolationLookahead();
    std::cout << "JitterBuffer tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
