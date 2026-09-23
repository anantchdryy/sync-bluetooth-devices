#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class AudioSampleFormat : std::uint8_t {
  PcmS16LittleEndian = 1,
};

struct AudioPacket {
  static constexpr std::uint8_t CurrentProtocolVersion = 2;

  std::uint8_t protocolVersion{CurrentProtocolVersion};
  std::uint16_t flags{};
  std::uint64_t sessionId{};
  std::uint32_t streamId{1};
  std::uint32_t sequenceNumber{};
  std::uint32_t sampleRate{};
  std::uint16_t channelCount{};
  AudioSampleFormat sampleFormat{AudioSampleFormat::PcmS16LittleEndian};
  std::uint64_t startFrame{};
  std::uint64_t presentationTimestampNanoseconds{};
  std::uint16_t frameCount{};
  std::vector<std::byte> pcmPayload;
};
