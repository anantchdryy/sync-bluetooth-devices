#include "AudioPacket.hpp"
#include "PacketSerializer.hpp"
#include "UdpAudioReceiver.hpp"
#include "UdpAudioSender.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

} // namespace

int main() {
  try {
    constexpr std::uint16_t port = 40'192;
    UdpAudioReceiver receiver("127.0.0.1", port,
                              std::chrono::milliseconds{1'000});
    UdpAudioSender sender("127.0.0.1", port);
    AudioPacket packet;
    packet.sessionId = 42;
    packet.sequenceNumber = 7;
    packet.sampleRate = 48'000;
    packet.channelCount = 1;
    packet.frameCount = 2;
    packet.pcmPayload = {std::byte{0x01}, std::byte{0x00},
                         std::byte{0x02}, std::byte{0x00}};
    sender.send(PacketSerializer::serialize(packet));
    const auto datagram = receiver.receive();
    require(datagram.has_value(), "No UDP audio packet arrived on loopback");
    const auto decoded = PacketSerializer::deserialize(*datagram);
    require(decoded.sessionId == packet.sessionId &&
                decoded.sequenceNumber == packet.sequenceNumber &&
                decoded.pcmPayload == packet.pcmPayload,
            "UDP audio packet changed during transport");
    std::cout << "Desktop transport tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
