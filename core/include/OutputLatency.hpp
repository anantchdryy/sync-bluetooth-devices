#pragma once

#include <chrono>
#include <string>

enum class OutputRouteType { Unknown, BuiltIn, Wired, Bluetooth, Usb };
enum class CalibrationConfidence { Unknown, SystemEstimate, Manual, Measured };

// Network clock offset and jitter-buffer delay are intentionally absent.
// This value models only the time from audio submission to physical sound.
struct OutputLatency {
  std::string outputRouteId;
  OutputRouteType outputRouteType{OutputRouteType::Unknown};
  CalibrationConfidence calibrationConfidence{CalibrationConfidence::Unknown};
  std::chrono::nanoseconds systemEstimate{};
  std::chrono::nanoseconds manualAdjustment{};

  [[nodiscard]] std::chrono::nanoseconds effectiveLatency() const;
  [[nodiscard]] std::chrono::steady_clock::time_point submissionTime(
      std::chrono::steady_clock::time_point acousticPresentationTime) const;
};
