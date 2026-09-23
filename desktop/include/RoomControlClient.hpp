#pragma once

#include <cstdint>
#include <string>

// Sends one host-only room command through the local TCP control channel.
class RoomControlClient {
public:
  [[nodiscard]] static std::string issue(std::uint16_t port,
                                          const std::string &command);
};
