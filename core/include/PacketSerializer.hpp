#pragma once

#include "AudioPacket.hpp"

#include <cstddef>
#include <span>
#include <vector>

class PacketSerializer {
public:
  static constexpr std::size_t HeaderSize = 52;
  static constexpr std::size_t MaximumDatagramSize = 1'200;
  static constexpr std::size_t MaximumPayloadSize =
      MaximumDatagramSize - HeaderSize;

  [[nodiscard]] static std::vector<std::byte>
  serialize(const AudioPacket &packet);
  [[nodiscard]] static AudioPacket deserialize(std::span<const std::byte> data);
};
