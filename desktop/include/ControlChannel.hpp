#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

class Room;

struct ControlStreamState {
  std::uint64_t sessionId{};
  std::atomic<std::uint32_t> streamId{1};
  std::uint16_t audioPort{40'100};
  std::uint16_t clockPort{40'101};
  std::string hostAddress;
  std::uint32_t sampleRate{};
  std::uint16_t channels{};
  std::uint64_t totalFrames{};
  std::atomic<std::uint64_t> currentFrame{0};
  std::atomic<bool> playing{false};
  Room *room{};
};

// Bounded line-oriented TCP control protocol. PCM remains on UDP.
class ControlServer {
public:
  ControlServer(std::uint16_t port, ControlStreamState &state);
  ~ControlServer();
  ControlServer(const ControlServer &) = delete;
  ControlServer &operator=(const ControlServer &) = delete;
  [[nodiscard]] std::uint16_t port() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
