#include "ClockSync.hpp"
#include "DriftCorrection.hpp"
#include "PacketSerializer.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}

void clockStress(double ppm) {
  DriftEstimator estimator;
  constexpr std::int64_t step = 2'000'000'000;
  constexpr std::int64_t base = 2'000'000'000'000;
  for (int index = 0; index < 60; ++index) {
    const auto elapsed = index * step;
    const auto smallNoise = index % 2 ? 80'000 : -80'000;
    const auto outlier = index == 20 ? 30'000'000 : 0;
    const auto offset = 40'000'000 +
        static_cast<std::int64_t>(ppm * static_cast<double>(elapsed) / 1'000'000.0) +
        smallNoise + outlier;
    estimator.addSample(base + elapsed, offset);
  }
  require(std::abs(estimator.estimatedDriftPpm() - ppm) < 5,
          "Clock regression did not reject an offset outlier");
  GradualDriftCorrector corrector;
  for (double error : {-100.0, -10.0, 0.0, 10.0, 100.0}) {
    const auto ratio = corrector.correctionRatio(ppm, error);
    require(ratio >= 0.9995 && ratio <= 1.0005,
            "Correction left its bounded range");
  }
}

void malformedTraffic() {
  std::mt19937 random(48152);
  std::uniform_int_distribution<int> length(0, 1600);
  std::uniform_int_distribution<int> octet(0, 255);
  for (int trial = 0; trial < 20'000; ++trial) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(length(random)));
    for (auto &byte : bytes) byte = static_cast<std::byte>(octet(random));
    try { (void)PacketSerializer::deserialize(bytes); }
    catch (const std::invalid_argument &) { continue; }
  }
  AudioPacket valid;
  valid.sessionId = 1;
  valid.streamId = 1;
  valid.sampleRate = 48'000;
  valid.channelCount = 1;
  valid.frameCount = 2;
  valid.pcmPayload.assign(4, std::byte{0});
  const auto encoded = PacketSerializer::serialize(valid);
  for (std::size_t index = 0; index < PacketSerializer::HeaderSize; ++index) {
    auto mutated = encoded;
    mutated[index] ^= std::byte{0xff};
    try { (void)PacketSerializer::deserialize(mutated); }
    catch (const std::invalid_argument &) { continue; }
  }
}
} // namespace

int main() {
  try {
    for (double ppm : {10.0, -10.0, 50.0, -50.0, 100.0, -100.0,
                       500.0, -500.0}) clockStress(ppm);
    malformedTraffic();
    std::cout << "Eight drift scenarios and 20,052 malformed datagrams passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
