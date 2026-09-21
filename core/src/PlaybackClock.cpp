#include "PlaybackClock.hpp"

#include <limits>
#include <stdexcept>

namespace {

constexpr std::uint64_t nanosecondsPerSecond = 1'000'000'000ULL;

} // namespace

PlaybackClock::PlaybackClock(std::uint32_t sampleRate, Timestamp startTime)
    : sampleRate_(sampleRate), startTime_(startTime) {
  if (sampleRate_ == 0) {
    throw std::invalid_argument("PlaybackClock sample rate must be non-zero");
  }
}

PlaybackClock::Timestamp PlaybackClock::now() noexcept {
  return std::chrono::time_point_cast<Duration>(Clock::now());
}

PlaybackClock::Timestamp PlaybackClock::startTime() const noexcept {
  return startTime_;
}

PlaybackClock::Duration PlaybackClock::elapsed() const noexcept {
  return elapsedAt(now());
}

PlaybackClock::Duration
PlaybackClock::elapsedAt(Timestamp timestamp) const noexcept {
  return timestamp - startTime_;
}

PlaybackClock::Timestamp PlaybackClock::frameToTimestamp(Frame frame) const {
  const auto offset = frameToDuration(frame);
  const auto startCount = startTime_.time_since_epoch().count();
  const auto maximum = std::numeric_limits<Duration::rep>::max();

  if (startCount > maximum - offset.count()) {
    throw std::overflow_error("Playback timestamp exceeds the supported range");
  }
  return Timestamp{Duration{startCount + offset.count()}};
}

PlaybackClock::Frame
PlaybackClock::timestampToFrame(Timestamp timestamp) const {
  if (timestamp <= startTime_) {
    return 0;
  }

  const auto nanoseconds = elapsedAt(timestamp).count();
  const auto wholeSeconds =
      static_cast<std::uint64_t>(nanoseconds) / nanosecondsPerSecond;
  const auto remainingNanoseconds =
      static_cast<std::uint64_t>(nanoseconds) % nanosecondsPerSecond;
  const auto maximum = std::numeric_limits<Frame>::max();

  if (wholeSeconds > maximum / sampleRate_) {
    throw std::overflow_error("Playback frame exceeds the supported range");
  }

  const auto wholeFrames = wholeSeconds * sampleRate_;
  const auto partialFrames =
      (remainingNanoseconds * sampleRate_) / nanosecondsPerSecond;
  if (partialFrames > maximum - wholeFrames) {
    throw std::overflow_error("Playback frame exceeds the supported range");
  }
  return wholeFrames + partialFrames;
}

std::uint32_t PlaybackClock::sampleRate() const noexcept { return sampleRate_; }

PlaybackClock::Duration PlaybackClock::frameToDuration(Frame frame) const {
  const auto wholeSeconds = frame / sampleRate_;
  const auto remainingFrames = frame % sampleRate_;
  const auto maximum =
      static_cast<std::uint64_t>(std::numeric_limits<Duration::rep>::max());

  if (wholeSeconds > maximum / nanosecondsPerSecond) {
    throw std::overflow_error("Playback duration exceeds the supported range");
  }

  const auto wholeNanoseconds = wholeSeconds * nanosecondsPerSecond;
  // Round a frame boundary upward so converting it back never selects the
  // preceding frame when the exact boundary is between two nanoseconds.
  const auto partialNanoseconds =
      (remainingFrames * nanosecondsPerSecond + sampleRate_ - 1U) / sampleRate_;
  if (partialNanoseconds > maximum - wholeNanoseconds) {
    throw std::overflow_error("Playback duration exceeds the supported range");
  }
  return Duration{
      static_cast<Duration::rep>(wholeNanoseconds + partialNanoseconds)};
}
