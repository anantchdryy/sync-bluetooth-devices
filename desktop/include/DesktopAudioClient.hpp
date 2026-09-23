#pragma once

#include <chrono>
#include <cstdint>
#include <string>

struct DesktopAudioClientConfig {
  std::string bindAddress{"0.0.0.0"};
  std::uint16_t audioPort{40'100};
  std::uint16_t clockSyncPort{40'101};
  std::chrono::milliseconds playbackDelay{20};
  std::chrono::milliseconds startupLead{40};
};

struct DesktopAudioClientStats {
  std::uint64_t startingFrame{};
  std::uint64_t finalPlaybackFrame{};
  std::uint64_t packetsReceived{};
  std::uint64_t packetsLost{};
  std::uint64_t outOfOrderPackets{};
  std::uint64_t latePackets{};
  std::uint64_t duplicatePackets{};
  double networkJitterMilliseconds{};
  double targetBufferMilliseconds{};
  double packetLossPercent{};
  double finalBufferDepthMilliseconds{};
  double peakBufferDepthMilliseconds{};
  double clockOffsetMilliseconds{};
  double clockRoundTripMilliseconds{};
  std::uint32_t clockSamples{};
  std::string clockMeasurementQuality;
  double estimatedDriftPpm{};
  double bufferErrorMilliseconds{};
  double correctionRatio{1.0};
  double estimatedPlaybackDelayMilliseconds{};
  std::uint64_t underrunFrames{};
};

class DesktopAudioClient {
public:
  explicit DesktopAudioClient(DesktopAudioClientConfig config);

  // Blocks until the current stream has finished playing.
  [[nodiscard]] DesktopAudioClientStats run(const std::string &hostAddress);

private:
  DesktopAudioClientConfig config_;
};
