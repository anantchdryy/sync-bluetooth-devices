#include "ClockSyncTransport.hpp"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void testSerialization() {
  ClockSyncMessage message;
  message.type = ClockSyncMessageType::Response;
  message.requestId = 0x11223344U;
  message.clientSendTimestampNanoseconds = 0x0102030405060708ULL;
  message.hostReceiveTimestampNanoseconds = 0x1112131415161718ULL;
  message.hostSendTimestampNanoseconds = 0x2122232425262728ULL;

  const auto bytes = ClockSyncSerializer::serialize(message);
  require(bytes[0] == std::byte{'S'} && bytes[1] == std::byte{'C'} &&
              bytes[2] == std::byte{'L'} && bytes[3] == std::byte{'K'},
          "Clock-sync magic is incorrect");
  require(bytes[4] == std::byte{1} && bytes[5] == std::byte{2},
          "Clock-sync version or type is incorrect");
  require(bytes[8] == std::byte{0x11} && bytes[11] == std::byte{0x44},
          "Clock-sync integer byte order is incorrect");

  const auto decoded = ClockSyncSerializer::deserialize(bytes);
  require(decoded.type == message.type &&
              decoded.requestId == message.requestId,
          "Clock-sync header did not round-trip");
  require(decoded.clientSendTimestampNanoseconds ==
                  message.clientSendTimestampNanoseconds &&
              decoded.hostReceiveTimestampNanoseconds ==
                  message.hostReceiveTimestampNanoseconds &&
              decoded.hostSendTimestampNanoseconds ==
                  message.hostSendTimestampNanoseconds,
          "Clock-sync timestamps did not round-trip");

  auto invalid = bytes;
  invalid[0] = std::byte{'X'};
  bool rejected = false;
  try {
    static_cast<void>(ClockSyncSerializer::deserialize(invalid));
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Invalid clock-sync magic must be rejected");
}

void testLoopbackEstimate() {
  constexpr std::uint16_t port = 40'191;
  ClockSyncServer server("127.0.0.1", port);
  const auto estimate = ClockSyncClient::measure("127.0.0.1", port, 3s, 4);
  require(estimate.samples == 4, "Clock sync did not collect all samples");
  require(estimate.roundTripTime >= 0ns && estimate.roundTripTime < 100ms,
          "Loopback clock-sync round trip is unreasonable");
  require(estimate.hostMinusClientOffset > -10ms &&
              estimate.hostMinusClientOffset < 10ms,
          "Same-process clock offset should be near zero");
}

} // namespace

int main() {
  try {
    testSerialization();
    testLoopbackEstimate();
    std::cout << "ClockSync tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
