#include "PlaybackClock.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {

using namespace std::chrono_literals;

static_assert(std::is_same_v<PlaybackClock::Clock, std::chrono::steady_clock>,
              "PlaybackClock must use the monotonic steady clock");

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

template <typename Exception, typename Function>
void requireThrows(Function function, std::string_view message) {
  try {
    function();
  } catch (const Exception &) {
    return;
  }
  throw std::runtime_error(std::string(message));
}

void testFrameTimeConversions() {
  const PlaybackClock::Timestamp start{123'456'789ns};
  const PlaybackClock clock(48'000, start);

  require(clock.sampleRate() == 48'000, "Sample rate was not retained");
  require(clock.startTime() == start, "Start timestamp changed");
  require(clock.frameToTimestamp(0) == start, "Frame zero must equal start");
  require(clock.frameToTimestamp(48'000) == start + 1s,
          "One second frame conversion failed");
  require(clock.frameToTimestamp(72'000) == start + 1500ms,
          "Fractional second frame conversion failed");
  require(clock.timestampToFrame(start + 1s) == 48'000,
          "One second timestamp conversion failed");
  require(clock.timestampToFrame(start + 1500ms) == 72'000,
          "Fractional timestamp conversion failed");
}

void testNonIntegralFrameBoundaries() {
  const PlaybackClock clock(44'100, PlaybackClock::Timestamp{});
  constexpr std::uint64_t frames[] = {1, 2, 44'099, 44'100, 123'456'789};

  for (const auto frame : frames) {
    const auto timestamp = clock.frameToTimestamp(frame);
    require(clock.timestampToFrame(timestamp) == frame,
            "Frame boundary did not round-trip");
  }
}

void testElapsedCalculations() {
  const PlaybackClock::Timestamp start{5s};
  const PlaybackClock clock(1'000, start);

  require(clock.elapsedAt(start) == 0ns, "Elapsed time at start must be zero");
  require(clock.elapsedAt(start + 250us) == 250us,
          "Sub-millisecond elapsed time was lost");
  require(clock.elapsedAt(start + 1234ms) == 1234ms,
          "Elapsed millisecond calculation failed");
  require(clock.elapsedAt(start - 10ms) == -10ms,
          "Pre-start elapsed time should remain negative");
}

void testEdgeCases() {
  requireThrows<std::invalid_argument>([] { PlaybackClock invalid(0); },
                                       "A zero sample rate must be rejected");

  const PlaybackClock::Timestamp start{10s};
  const PlaybackClock clock(48'000, start);
  require(clock.timestampToFrame(start - 1ns) == 0,
          "A pre-start timestamp must map to frame zero");
  require(clock.timestampToFrame(start + 20'833ns) == 0,
          "A timestamp before frame one must map to frame zero");
  require(clock.timestampToFrame(start + 20'834ns) == 1,
          "The rounded frame-one boundary must map to frame one");
  requireThrows<std::overflow_error>(
      [&clock] {
        static_cast<void>(
            clock.frameToTimestamp(std::numeric_limits<std::uint64_t>::max()));
      },
      "An unrepresentable timestamp must be rejected");
}

} // namespace

int main() {
  try {
    testFrameTimeConversions();
    testNonIntegralFrameBoundaries();
    testElapsedCalculations();
    testEdgeCases();
    std::cout << "PlaybackClock tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
