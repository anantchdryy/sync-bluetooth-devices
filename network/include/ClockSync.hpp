#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

enum class ClockSyncMessageType : std::uint8_t {
  Request = 1,
  Response = 2,
};

struct ClockSyncMessage {
  static constexpr std::uint8_t ProtocolVersion = 1;

  ClockSyncMessageType type{ClockSyncMessageType::Request};
  std::uint32_t requestId{};
  std::uint64_t clientSendTimestampNanoseconds{};  // t1
  std::uint64_t hostReceiveTimestampNanoseconds{}; // t2
  std::uint64_t hostSendTimestampNanoseconds{};    // t3
};

struct ClockSyncEstimate {
  // Add this value to a client timestamp to estimate the host timestamp.
  std::chrono::nanoseconds hostMinusClientOffset{};
  std::chrono::nanoseconds roundTripTime{};
  std::uint32_t samples{};
  std::int64_t clientSampleTimestampNanoseconds{};
};

class ClockSyncSerializer {
public:
  static constexpr std::size_t MessageSize = 40;

  [[nodiscard]] static std::array<std::byte, MessageSize>
  serialize(const ClockSyncMessage &message);
  [[nodiscard]] static ClockSyncMessage
  deserialize(std::span<const std::byte> data);
};

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
