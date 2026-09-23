#pragma once

#include "AudioPacket.hpp"

#include <cstddef>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

struct BufferedPacketTiming {
  std::uint64_t startFrame{};
  std::uint64_t presentationTimestampNanoseconds{};
};

struct JitterBufferFormat {
  std::uint32_t sampleRate{};
  std::uint16_t channelCount{};
};

struct JitterBufferMetrics {
  double networkJitterMilliseconds{};
  double targetBufferMilliseconds{180.0};
  double actualBufferMilliseconds{};
  std::uint64_t latePackets{};
  std::uint64_t duplicatePackets{};
  std::uint64_t overflowPackets{};
};

class JitterBuffer {
public:
  // Returns false for a duplicate starting frame.
  bool push(const AudioPacket &packet);

  // Missing frames remain silent. The return value is the number of frames
  // copied from received packets.
  std::uint32_t readFrames(std::uint64_t startFrame,
                           std::span<std::int16_t> interleavedOutput,
                           bool retainLookahead = false);

  [[nodiscard]] std::optional<BufferedPacketTiming>
  firstPacketAtOrAfter(std::uint64_t hostTimestampNanoseconds) const;
  [[nodiscard]] std::optional<JitterBufferFormat> format() const;
  [[nodiscard]] std::uint64_t latestEndFrame() const;
  [[nodiscard]] std::uint64_t depthFrames(std::uint64_t playbackFrame) const;
  [[nodiscard]] std::size_t packetCount() const;
  [[nodiscard]] JitterBufferMetrics metrics(std::uint64_t playbackFrame) const;

private:
  struct BufferedPacket {
    std::uint64_t startFrame{};
    std::uint64_t presentationTimestampNanoseconds{};
    std::uint16_t frameCount{};
    std::vector<std::int16_t> samples;
  };

  mutable std::mutex mutex_;
  std::map<std::uint64_t, BufferedPacket> packets_;
  std::optional<JitterBufferFormat> format_;
  std::uint64_t latestEndFrame_{};
  std::atomic<std::uint64_t> latestEndFrameAtomic_{0};
  std::uint64_t latestReadFrame_{};
  std::uint64_t lastPresentationNanoseconds_{};
  std::chrono::steady_clock::time_point lastArrival_{};
  JitterBufferMetrics metrics_{};
};
