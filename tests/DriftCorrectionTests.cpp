#include "DriftCorrection.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void simulate(double hostDriftPpm, double deviceDriftPpm) {
  DriftEstimator estimator;
  GradualDriftCorrector corrector;
  constexpr std::int64_t baseTime = 1'000'000'000'000LL;
  constexpr std::int64_t baseOffset = 50'000'000LL;
  double desiredFrames = 0.0;
  double playedFrames = 0.0;
  constexpr double sampleRate = 48'000.0;

  for (int step = 0; step < 6'000; ++step) {
    // A clock-sync exchange every five seconds, with small measurement noise.
    if (step % 50 == 0) {
      const std::int64_t elapsed = static_cast<std::int64_t>(step) * 100'000'000LL;
      const std::int64_t noise = (step / 50 % 2 == 0) ? 20'000 : -20'000;
      estimator.addSample(baseTime + elapsed,
                          baseOffset + static_cast<std::int64_t>(
                                           hostDriftPpm * elapsed / 1'000'000.0) +
                              noise);
    }
    const double errorMs = (desiredFrames - playedFrames) * 1'000.0 / sampleRate;
    const double ratio = corrector.correctionRatio(
        estimator.estimatedDriftPpm(), errorMs);
    require(ratio >= 0.9995 && ratio <= 1.0005,
            "Correction exceeded the subtle 500 ppm limit");
    desiredFrames += sampleRate * 0.1 * (1.0 + hostDriftPpm / 1'000'000.0);
    playedFrames += sampleRate * 0.1 *
                    (1.0 + deviceDriftPpm / 1'000'000.0) * ratio;
  }

  require(std::abs(estimator.estimatedDriftPpm() - hostDriftPpm) < 5.0,
          "Simulated clock-rate difference was not estimated accurately");
  const double finalErrorMs =
      (desiredFrames - playedFrames) * 1'000.0 / sampleRate;
  require(std::abs(finalErrorMs) < 5.0,
          "Playback drift was not gradually corrected");
}

} // namespace

int main() {
  try {
    simulate(120.0, -60.0);
    simulate(-120.0, 60.0);
    std::cout << "Drift correction tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
