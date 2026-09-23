#pragma once

#include "PlaybackClock.hpp"
#ifndef NDEBUG
#include "NetworkImpairment.hpp"
#include <optional>
#endif

#include <chrono>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

struct DesktopNetworkHostConfig {
  std::string destinationAddress{"255.255.255.255"};
  std::uint16_t port{40'100};
  std::chrono::milliseconds desiredPacketDuration{10};
  std::chrono::milliseconds sendAhead{500};
  std::chrono::milliseconds hostOutputLatency{};
  std::uint64_t sessionId{};
  std::atomic<std::uint64_t> *progressFrame{};
  std::function<bool()> outputRouteChanged;
#ifndef NDEBUG
  std::optional<NetworkImpairmentConfig> impairment;
#endif
};

struct DesktopNetworkHostStats {
  std::uint64_t sessionId{};
  std::uint64_t packetsSent{};
  std::uint64_t framesSent{};
  std::uint64_t audioDatagramBytesSent{};
  double sendDurationSeconds{};
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
