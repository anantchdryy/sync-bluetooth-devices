#include "DriftCorrection.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

void DriftEstimator::addSample(std::int64_t time, std::int64_t offset) {
  if (!samples_.empty() && time <= samples_.back().time) {
    throw std::invalid_argument("Clock samples must have increasing timestamps");
  }
  if (samples_.size() >= 4) {
    const auto residual = std::abs(static_cast<double>(offset) -
                                   offsetNanosecondsAt(time));
    const auto elapsed = static_cast<double>(time - samples_.back().time);
    const auto limit = std::max(5'000'000.0, elapsed * 0.001);
    if (residual > limit) {
      if (++consecutiveOutliers_ < 3) return;
      // Repeated shifted samples indicate a discontinuity rather than noise.
      samples_.clear();
    } else consecutiveOutliers_ = 0;
  }
  if (samples_.empty()) consecutiveOutliers_ = 0;
  samples_.push_back({time, offset});
  // Keep recent history so an old measurement does not dominate forever.
  while (samples_.size() > 32 ||
         (samples_.size() > 2 && time - samples_.front().time >
                                     120'000'000'000LL)) {
    samples_.pop_front();
  }
  if (samples_.size() < 2) {
    slope_ = 0.0;
    fittedOffsetAtLast_ = static_cast<double>(offset);
    return;
  }

  const double originTime = static_cast<double>(samples_.front().time);
  const double originOffset = static_cast<double>(samples_.front().offset);
  double sumTime = 0.0;
  double sumOffset = 0.0;
  for (const auto &sample : samples_) {
    sumTime += static_cast<double>(sample.time) - originTime;
    sumOffset += static_cast<double>(sample.offset) - originOffset;
  }
  const double meanTime = sumTime / static_cast<double>(samples_.size());
  const double meanOffset = sumOffset / static_cast<double>(samples_.size());
  double covariance = 0.0;
  double variance = 0.0;
  for (const auto &sample : samples_) {
    const double x = static_cast<double>(sample.time) - originTime - meanTime;
    const double y = static_cast<double>(sample.offset) - originOffset - meanOffset;
    covariance += x * y;
    variance += x * x;
  }
  slope_ = variance > 0.0 ? std::clamp(covariance / variance, -0.002, 0.002)
                          : 0.0;
  fittedOffsetAtLast_ = originOffset + meanOffset +
                        slope_ * (static_cast<double>(time) - originTime - meanTime);
}

double DriftEstimator::offsetNanosecondsAt(std::int64_t time) const noexcept {
  if (samples_.empty()) {
    return 0.0;
  }
  return fittedOffsetAtLast_ +
         slope_ * static_cast<double>(time - samples_.back().time);
}

double DriftEstimator::estimatedDriftPpm() const noexcept {
  return slope_ * 1'000'000.0;
}

std::size_t DriftEstimator::sampleCount() const noexcept {
  return samples_.size();
}

double GradualDriftCorrector::correctionRatio(double driftPpm,
                                               double bufferErrorMs) const {
  if (!std::isfinite(driftPpm) || !std::isfinite(bufferErrorMs)) {
    return 1.0;
  }
  // Twenty ppm per millisecond removes a 10 ms phase error over roughly 50 s.
  const double correctionPpm = std::clamp(
      driftPpm + 20.0 * bufferErrorMs, -MaximumCorrectionPpm,
      MaximumCorrectionPpm);
  return 1.0 + correctionPpm / 1'000'000.0;
}
