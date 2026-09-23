#include "OutputLatency.hpp"

#include <stdexcept>

std::chrono::nanoseconds OutputLatency::effectiveLatency() const {
  using namespace std::chrono;
  if (systemEstimate < 0ns || systemEstimate > 5s ||
      manualAdjustment < -1s || manualAdjustment > 1s)
    throw std::invalid_argument("Output latency estimate is outside supported range");
  return systemEstimate + manualAdjustment;
}

std::chrono::steady_clock::time_point OutputLatency::submissionTime(
    std::chrono::steady_clock::time_point acousticPresentationTime) const {
  return acousticPresentationTime - effectiveLatency();
}
