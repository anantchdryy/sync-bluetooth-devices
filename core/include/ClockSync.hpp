#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

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

class ClockSyncMath {
public:
  // Four monotonic timestamps from one request/response exchange.
  [[nodiscard]] static ClockSyncEstimate
  estimate(std::uint64_t clientSendNanoseconds,
           std::uint64_t hostReceiveNanoseconds,
           std::uint64_t hostSendNanoseconds,
           std::uint64_t clientReceiveNanoseconds);
};
