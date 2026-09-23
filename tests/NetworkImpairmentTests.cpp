#include "NetworkImpairment.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

std::size_t run(NetworkImpairmentConfig config) {
  NetworkImpairment simulator(config);
  const std::array<std::byte, 1> datagram{std::byte{0x7f}};
  std::size_t delivered = 0;
  for (int index = 0; index < 100; ++index) {
    const auto now = std::chrono::milliseconds(index * 10);
    simulator.submit(datagram, now);
    delivered += simulator.drain(now).size();
  }
  delivered += simulator.drain(std::chrono::seconds(30)).size();
  require(simulator.pending() == 0, "Delayed datagrams were not drained");
  return delivered;
}
} // namespace

int main() {
  try {
    for (double loss : {0.0, 1.0, 5.0, 10.0})
      for (std::uint32_t jitter : {0U, 5U, 20U, 50U, 100U})
        for (std::uint32_t delay : {0U, 20U, 50U, 100U, 250U}) {
          NetworkImpairmentConfig config;
          config.packetLossPercent = loss;
          config.jitterMs = jitter;
          config.baseDelayMs = delay;
          config.seed = 493;
          require(run(config) == run(config), "Fixed-seed simulation changed");
        }
    for (std::uint32_t blackout : {100U, 500U, 1'000U, 2'000U, 5'000U}) {
      NetworkImpairmentConfig config;
      config.blackoutDurationMs = blackout;
      require(run(config) == (blackout == 100 ? 90U :
                              blackout == 500 ? 50U : 0U),
              "Blackout delivery count is wrong");
    }
    NetworkImpairmentConfig duplicate;
    duplicate.duplicatePercent = 100;
    require(run(duplicate) == 200, "Duplication did not double packets");
    NetworkImpairmentConfig fullLoss;
    fullLoss.packetLossPercent = 100;
    require(run(fullLoss) == 0, "Full loss delivered packets");

    NetworkImpairmentConfig reorder;
    reorder.reorderPercent = 50;
    NetworkImpairment simulator(reorder);
    const std::array<std::byte, 1> payload{std::byte{1}};
    std::vector<std::uint64_t> order;
    for (int index = 0; index < 100; ++index) {
      auto now = std::chrono::milliseconds(index * 10);
      simulator.submit(payload, now);
      for (const auto &packet : simulator.drain(now)) order.push_back(packet.sequence);
    }
    for (const auto &packet : simulator.drain(std::chrono::seconds(30)))
      order.push_back(packet.sequence);
    require(order.size() == 100, "Reordering lost packets");
    bool outOfOrder = false;
    for (std::size_t index = 1; index < order.size(); ++index)
      if (order[index] < order[index - 1]) outOfOrder = true;
    require(outOfOrder, "Reorder simulation did not reorder");
    std::cout << "Network impairment matrix passed (100 latency/loss/jitter cases; "
                 "5 blackouts, duplication and reordering)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
