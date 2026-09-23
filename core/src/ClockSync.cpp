#include "ClockSync.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

constexpr std::array<std::byte, 4> magic{std::byte{'S'}, std::byte{'C'},
                                         std::byte{'L'}, std::byte{'K'}};

void appendU8(std::span<std::byte> output, std::size_t &offset,
              std::uint8_t value) {
  output[offset++] = static_cast<std::byte>(value);
}

void appendU16(std::span<std::byte> output, std::size_t &offset,
               std::uint16_t value) {
  appendU8(output, offset, static_cast<std::uint8_t>(value >> 8U));
  appendU8(output, offset, static_cast<std::uint8_t>(value));
}

void appendU32(std::span<std::byte> output, std::size_t &offset,
               std::uint32_t value) {
  appendU16(output, offset, static_cast<std::uint16_t>(value >> 16U));
  appendU16(output, offset, static_cast<std::uint16_t>(value));
}

void appendU64(std::span<std::byte> output, std::size_t &offset,
               std::uint64_t value) {
  appendU32(output, offset, static_cast<std::uint32_t>(value >> 32U));
  appendU32(output, offset, static_cast<std::uint32_t>(value));
}

std::uint8_t readU8(std::span<const std::byte> data, std::size_t &offset) {
  return std::to_integer<std::uint8_t>(data[offset++]);
}

std::uint16_t readU16(std::span<const std::byte> data, std::size_t &offset) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(readU8(data, offset)) << 8U) |
      readU8(data, offset));
}

std::uint32_t readU32(std::span<const std::byte> data, std::size_t &offset) {
  return (static_cast<std::uint32_t>(readU16(data, offset)) << 16U) |
         readU16(data, offset);
}

std::uint64_t readU64(std::span<const std::byte> data, std::size_t &offset) {
  return (static_cast<std::uint64_t>(readU32(data, offset)) << 32U) |
         readU32(data, offset);
}

} // namespace

std::array<std::byte, ClockSyncSerializer::MessageSize>
ClockSyncSerializer::serialize(const ClockSyncMessage &message) {
  if (message.type != ClockSyncMessageType::Request &&
      message.type != ClockSyncMessageType::Response) {
    throw std::invalid_argument("Clock-sync message type is invalid");
  }

  std::array<std::byte, MessageSize> output{};
  std::copy(magic.begin(), magic.end(), output.begin());
  std::size_t offset = magic.size();
  appendU8(output, offset, ClockSyncMessage::ProtocolVersion);
  appendU8(output, offset, static_cast<std::uint8_t>(message.type));
  appendU16(output, offset, static_cast<std::uint16_t>(MessageSize));
  appendU32(output, offset, message.requestId);
  appendU32(output, offset, 0); // Reserved.
  appendU64(output, offset, message.clientSendTimestampNanoseconds);
  appendU64(output, offset, message.hostReceiveTimestampNanoseconds);
  appendU64(output, offset, message.hostSendTimestampNanoseconds);
  return output;
}

ClockSyncMessage
ClockSyncSerializer::deserialize(std::span<const std::byte> data) {
  if (data.size() != MessageSize ||
      !std::equal(magic.begin(), magic.end(), data.begin())) {
    throw std::invalid_argument("Clock-sync message header is invalid");
  }
  std::size_t offset = magic.size();
  if (readU8(data, offset) != ClockSyncMessage::ProtocolVersion) {
    throw std::invalid_argument("Clock-sync protocol version is unsupported");
  }
  ClockSyncMessage result;
  result.type = static_cast<ClockSyncMessageType>(readU8(data, offset));
  if (result.type != ClockSyncMessageType::Request &&
      result.type != ClockSyncMessageType::Response) {
    throw std::invalid_argument("Clock-sync message type is invalid");
  }
  if (readU16(data, offset) != MessageSize) {
    throw std::invalid_argument("Clock-sync message size is invalid");
  }
  result.requestId = readU32(data, offset);
  static_cast<void>(readU32(data, offset));
  result.clientSendTimestampNanoseconds = readU64(data, offset);
  result.hostReceiveTimestampNanoseconds = readU64(data, offset);
  result.hostSendTimestampNanoseconds = readU64(data, offset);
  return result;
}

ClockSyncEstimate ClockSyncMath::estimate(
    std::uint64_t clientSend, std::uint64_t hostReceive,
    std::uint64_t hostSend, std::uint64_t clientReceive) {
  constexpr auto maximum =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if (clientSend > maximum || hostReceive > maximum || hostSend > maximum ||
      clientReceive > maximum || clientReceive < clientSend ||
      hostSend < hostReceive) {
    throw std::invalid_argument("Clock-sync timestamps are invalid");
  }
  const auto t1 = static_cast<long double>(clientSend);
  const auto t2 = static_cast<long double>(hostReceive);
  const auto t3 = static_cast<long double>(hostSend);
  const auto t4 = static_cast<long double>(clientReceive);
  const auto roundTrip = (t4 - t1) - (t3 - t2);
  const auto offset = ((t2 - t1) + (t3 - t4)) / 2.0L;
  if (roundTrip < 0 ||
      roundTrip > static_cast<long double>(maximum) ||
      std::abs(offset) > static_cast<long double>(maximum)) {
    throw std::invalid_argument("Clock-sync timing sample is invalid");
  }
  return ClockSyncEstimate{
      std::chrono::nanoseconds{static_cast<std::int64_t>(std::llround(offset))},
      std::chrono::nanoseconds{static_cast<std::int64_t>(std::llround(roundTrip))},
      1,
      static_cast<std::int64_t>(clientSend +
                                (clientReceive - clientSend) / 2)};
}
