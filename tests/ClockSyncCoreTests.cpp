#include "ClockSync.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void testTimingMath() {
  const auto estimate = ClockSyncMath::estimate(1'000, 1'150, 1'160, 1'050);
  require(estimate.hostMinusClientOffset == std::chrono::nanoseconds{130},
          "Clock offset is incorrect");
  require(estimate.roundTripTime == std::chrono::nanoseconds{40},
          "Round-trip time is incorrect");
  require(estimate.clientSampleTimestampNanoseconds == 1'025,
          "Sample midpoint is incorrect");
}

void testInvalidTiming() {
  bool rejected = false;
  try {
    static_cast<void>(ClockSyncMath::estimate(1'000, 1'100, 1'120, 1'010));
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Impossible negative round trip should be rejected");
}

} // namespace

int main() {
  try {
    testTimingMath();
    testInvalidTiming();
    std::cout << "ClockSync core tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
