#pragma once

#include "ClockSync.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

// Desktop UDP transport for the portable clock-sync protocol.
class ClockSyncServer {
public:
  ClockSyncServer(std::string bindAddress, std::uint16_t port);
  ~ClockSyncServer();

  ClockSyncServer(const ClockSyncServer &) = delete;
  ClockSyncServer &operator=(const ClockSyncServer &) = delete;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class ClockSyncClient {
public:
  [[nodiscard]] static ClockSyncEstimate
  measure(const std::string &hostAddress, std::uint16_t port,
          std::chrono::seconds overallTimeout = std::chrono::seconds{30},
          std::uint32_t desiredSamples = 8);
};
