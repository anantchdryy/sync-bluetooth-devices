#pragma once

#include "PlaybackClock.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

struct DesktopNetworkHostConfig {
  std::string destinationAddress{"255.255.255.255"};
  std::uint16_t port{40'100};
  std::chrono::milliseconds desiredPacketDuration{10};
  std::chrono::milliseconds sendAhead{500};
};

struct DesktopNetworkHostStats {
  std::uint64_t sessionId{};
  std::uint64_t packetsSent{};
  std::uint64_t framesSent{};
  std::uint16_t framesPerPacket{};
  double packetDurationMilliseconds{};
};

class DesktopNetworkHost {
public:
  explicit DesktopNetworkHost(DesktopNetworkHostConfig config);

  [[nodiscard]] DesktopNetworkHostStats
  streamFile(const std::filesystem::path &path,
             PlaybackClock::Timestamp playbackStartTime,
             std::uint32_t expectedSampleRate,
             std::uint16_t expectedChannelCount);

  [[nodiscard]] const DesktopNetworkHostConfig &config() const noexcept;

private:
  DesktopNetworkHostConfig config_;
};
