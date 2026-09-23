#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

// Samples describe host steady-clock time minus client steady-clock time.
// All timestamps use nanoseconds from the client's monotonic clock.
class DriftEstimator {
public:
  void addSample(std::int64_t clientTimestampNanoseconds,
                 std::int64_t hostMinusClientNanoseconds);
  [[nodiscard]] double offsetNanosecondsAt(
      std::int64_t clientTimestampNanoseconds) const noexcept;
  [[nodiscard]] double estimatedDriftPpm() const noexcept;
  [[nodiscard]] std::size_t sampleCount() const noexcept;

private:
  struct Sample {
    std::int64_t time;
    std::int64_t offset;
  };
  std::deque<Sample> samples_;
  double slope_{};
  double fittedOffsetAtLast_{};
  std::uint32_t consecutiveOutliers_{};
};

class DriftCorrector {
public:
  virtual ~DriftCorrector() = default;
  // Ratio of source frames consumed per output frame. One means no correction.
  [[nodiscard]] virtual double correctionRatio(double estimatedDriftPpm,
                                                double bufferErrorMs) const = 0;
};

class GradualDriftCorrector final : public DriftCorrector {
public:
  [[nodiscard]] double correctionRatio(double estimatedDriftPpm,
                                        double bufferErrorMs) const override;
  static constexpr double MaximumCorrectionPpm = 500.0;
};
