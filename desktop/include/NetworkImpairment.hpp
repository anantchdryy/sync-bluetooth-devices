#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <span>
#include <vector>

// Debug-only packet-path simulator. Times are supplied by the caller so tests
// never sleep and a fixed seed produces the same result on every run.
struct NetworkImpairmentConfig {
  double packetLossPercent{};
  std::uint32_t baseDelayMs{};
  std::uint32_t jitterMs{};
  double duplicatePercent{};
  double reorderPercent{};
  std::uint32_t blackoutDurationMs{};
  std::uint32_t seed{1};
};

struct ImpairedDatagram {
  std::chrono::milliseconds deliveryTime{};
  std::vector<std::byte> bytes;
  std::uint64_t sequence{};
};

class NetworkImpairment {
public:
  explicit NetworkImpairment(NetworkImpairmentConfig config);
  void submit(std::span<const std::byte> datagram,
              std::chrono::milliseconds now);
  [[nodiscard]] std::vector<ImpairedDatagram>
  drain(std::chrono::milliseconds now);
  [[nodiscard]] std::size_t pending() const noexcept { return pending_.size(); }

private:
  NetworkImpairmentConfig config_;
  std::mt19937 random_;
  std::deque<ImpairedDatagram> pending_;
  std::uint64_t nextSequence_{};
  bool started_{};
  std::chrono::milliseconds startTime_{};
  bool chance(double percent);
};
