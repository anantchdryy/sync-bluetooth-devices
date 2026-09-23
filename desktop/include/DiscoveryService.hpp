#pragma once

#include <cstdint>
#include <memory>
#include <string>

// Advertises the host's TCP control endpoint via DNS-SD on the local LAN.
class DiscoveryService {
public:
  explicit DiscoveryService(std::uint16_t controlPort);
  ~DiscoveryService();
  DiscoveryService(const DiscoveryService &) = delete;
  DiscoveryService &operator=(const DiscoveryService &) = delete;
  [[nodiscard]] const std::string &hostAddress() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
