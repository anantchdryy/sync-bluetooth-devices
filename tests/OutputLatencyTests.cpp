#include "OutputLatency.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

int main() {
  using namespace std::chrono;
  try {
    OutputLatency speaker{"builtin", OutputRouteType::BuiltIn,
                          CalibrationConfidence::Manual, 35ms, 10ms};
    OutputLatency bluetooth{"headset", OutputRouteType::Bluetooth,
                            CalibrationConfidence::Manual, 180ms, -20ms};
    const auto desired = steady_clock::time_point{seconds(10)};
    if (speaker.submissionTime(desired) != desired - 45ms ||
        bluetooth.submissionTime(desired) != desired - 160ms ||
        bluetooth.outputRouteId == speaker.outputRouteId)
      throw std::runtime_error("Route-specific acoustic scheduling failed");
    bluetooth.manualAdjustment = 1001ms;
    try {
      (void)bluetooth.effectiveLatency();
      throw std::runtime_error("Out-of-range calibration was accepted");
    } catch (const std::invalid_argument &) {}
    std::cout << "Output latency model tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
