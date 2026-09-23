#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class UdpAudioReceiver {
public:
  UdpAudioReceiver(std::string bindAddress, std::uint16_t port,
                   std::chrono::milliseconds timeout);
  ~UdpAudioReceiver();

  UdpAudioReceiver(const UdpAudioReceiver &) = delete;
  UdpAudioReceiver &operator=(const UdpAudioReceiver &) = delete;
  UdpAudioReceiver(UdpAudioReceiver &&) noexcept;
  UdpAudioReceiver &operator=(UdpAudioReceiver &&) noexcept;

  [[nodiscard]] std::optional<std::vector<std::byte>> receive() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
