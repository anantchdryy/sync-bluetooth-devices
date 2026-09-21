#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

class UdpAudioSender {
public:
  UdpAudioSender(std::string destinationAddress, std::uint16_t port);
  ~UdpAudioSender();

  UdpAudioSender(const UdpAudioSender &) = delete;
  UdpAudioSender &operator=(const UdpAudioSender &) = delete;
  UdpAudioSender(UdpAudioSender &&) noexcept;
  UdpAudioSender &operator=(UdpAudioSender &&) noexcept;

  void send(std::span<const std::byte> datagram) const;

  [[nodiscard]] const std::string &destinationAddress() const noexcept;
  [[nodiscard]] std::uint16_t port() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
