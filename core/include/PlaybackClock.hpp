#pragma once

#include <chrono>
#include <cstdint>

class PlaybackClock {
public:
  using Clock = std::chrono::steady_clock;
  using Duration = std::chrono::nanoseconds;
  using Timestamp = std::chrono::time_point<Clock, Duration>;
  using Frame = std::uint64_t;

  explicit PlaybackClock(std::uint32_t sampleRate, Timestamp startTime = now());

  [[nodiscard]] static Timestamp now() noexcept;
  [[nodiscard]] Timestamp startTime() const noexcept;
  [[nodiscard]] Duration elapsed() const noexcept;
  [[nodiscard]] Duration elapsedAt(Timestamp timestamp) const noexcept;
  [[nodiscard]] Timestamp frameToTimestamp(Frame frame) const;
  [[nodiscard]] Frame timestampToFrame(Timestamp timestamp) const;
  [[nodiscard]] std::uint32_t sampleRate() const noexcept;

private:
  [[nodiscard]] Duration frameToDuration(Frame frame) const;

  std::uint32_t sampleRate_;
  Timestamp startTime_;
};
