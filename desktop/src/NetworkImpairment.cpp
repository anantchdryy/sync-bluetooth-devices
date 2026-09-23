#include "NetworkImpairment.hpp"

#include <algorithm>
#include <stdexcept>

namespace {
void checkPercent(double value) {
  if (!(value >= 0.0 && value <= 100.0))
    throw std::invalid_argument("Impairment percent must be within 0..100");
}
} // namespace

NetworkImpairment::NetworkImpairment(NetworkImpairmentConfig config)
    : config_(config), random_(config.seed) {
  checkPercent(config.packetLossPercent);
  checkPercent(config.duplicatePercent);
  checkPercent(config.reorderPercent);
  if (config.baseDelayMs > 10'000 || config.jitterMs > 10'000 ||
      config.blackoutDurationMs > 60'000)
    throw std::invalid_argument("Impairment duration exceeds debug limit");
}

bool NetworkImpairment::chance(double percent) {
  if (percent <= 0.0) return false;
  if (percent >= 100.0) return true;
  return std::uniform_real_distribution<double>(0.0, 100.0)(random_) < percent;
}

void NetworkImpairment::submit(std::span<const std::byte> datagram,
                               std::chrono::milliseconds now) {
  if (!started_) { started_ = true; startTime_ = now; }
  if (now - startTime_ < std::chrono::milliseconds(config_.blackoutDurationMs) ||
      chance(config_.packetLossPercent)) return;

  const auto count = chance(config_.duplicatePercent) ? 2 : 1;
  for (int copy = 0; copy < count; ++copy) {
    const auto jitter = config_.jitterMs == 0 ? 0 :
        std::uniform_int_distribution<std::uint32_t>(0, config_.jitterMs)(random_);
    const auto extra = chance(config_.reorderPercent) ? 30U : 0U;
    pending_.push_back(ImpairedDatagram{
        now + std::chrono::milliseconds(config_.baseDelayMs + jitter + extra),
        std::vector<std::byte>(datagram.begin(), datagram.end()), nextSequence_++});
  }
}

std::vector<ImpairedDatagram>
NetworkImpairment::drain(std::chrono::milliseconds now) {
  std::vector<ImpairedDatagram> ready;
  for (auto iterator = pending_.begin(); iterator != pending_.end();) {
    if (iterator->deliveryTime <= now) {
      ready.push_back(std::move(*iterator));
      iterator = pending_.erase(iterator);
    } else ++iterator;
  }
  std::stable_sort(ready.begin(), ready.end(), [](const auto &left, const auto &right) {
    return left.deliveryTime == right.deliveryTime
        ? left.sequence < right.sequence
        : left.deliveryTime < right.deliveryTime;
  });
  return ready;
}
