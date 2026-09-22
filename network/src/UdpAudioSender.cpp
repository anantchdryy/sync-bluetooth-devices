#include "UdpAudioSender.hpp"

#include "PacketSerializer.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WS2tcpip.h>
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle invalidSocket = INVALID_SOCKET;

std::string socketError() {
  return "Winsock error " + std::to_string(WSAGetLastError());
}

void closeSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle invalidSocket = -1;

std::string socketError() { return std::strerror(errno); }

void closeSocket(SocketHandle socket) { close(socket); }
#endif

} // namespace

class UdpAudioSender::Impl {
public:
  Impl(std::string destinationAddress, std::uint16_t port)
      : destinationAddress_(std::move(destinationAddress)), port_(port) {
    if (port_ == 0) {
      throw std::invalid_argument("UDP destination port must be non-zero");
    }

#ifdef _WIN32
    WSADATA winsockData{};
    const auto startupResult = WSAStartup(MAKEWORD(2, 2), &winsockData);
    if (startupResult != 0) {
      throw std::runtime_error("Unable to initialize Winsock: error " +
                               std::to_string(startupResult));
    }
    winsockInitialized_ = true;
#endif

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == invalidSocket) {
      const auto error = socketError();
      cleanup();
      throw std::runtime_error("Unable to create UDP socket: " + error);
    }

    const int enabled = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST,
                   reinterpret_cast<const char *>(&enabled),
                   sizeof(enabled)) != 0) {
      const auto error = socketError();
      cleanup();
      throw std::runtime_error("Unable to enable UDP broadcast: " + error);
    }

    destination_.sin_family = AF_INET;
    destination_.sin_port = htons(port_);
    if (inet_pton(AF_INET, destinationAddress_.c_str(),
                  &destination_.sin_addr) != 1) {
      cleanup();
      throw std::invalid_argument(
          "UDP destination must be a numeric IPv4 address");
    }
  }

  ~Impl() { cleanup(); }

  void send(std::span<const std::byte> datagram) const {
    if (datagram.empty() ||
        datagram.size() > PacketSerializer::MaximumDatagramSize) {
      throw std::invalid_argument("UDP audio datagram size is invalid");
    }

#ifdef _WIN32
    const auto size = static_cast<int>(datagram.size());
#else
    const auto size = datagram.size();
#endif
    const auto sent =
        sendto(socket_, reinterpret_cast<const char *>(datagram.data()), size,
               0, reinterpret_cast<const sockaddr *>(&destination_),
#ifdef _WIN32
               static_cast<int>(sizeof(destination_)));
#else
               static_cast<socklen_t>(sizeof(destination_)));
#endif
    if (sent < 0 || static_cast<std::size_t>(sent) != datagram.size()) {
      throw std::runtime_error("Unable to send UDP audio packet: " +
                               socketError());
    }
  }

  [[nodiscard]] const std::string &destinationAddress() const noexcept {
    return destinationAddress_;
  }

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

private:
  void cleanup() noexcept {
    if (socket_ != invalidSocket) {
      closeSocket(socket_);
      socket_ = invalidSocket;
    }
#ifdef _WIN32
    if (winsockInitialized_) {
      WSACleanup();
      winsockInitialized_ = false;
    }
#endif
  }

  std::string destinationAddress_;
  std::uint16_t port_{};
  SocketHandle socket_{invalidSocket};
  sockaddr_in destination_{};
#ifdef _WIN32
  bool winsockInitialized_{false};
#endif
};

UdpAudioSender::UdpAudioSender(std::string destinationAddress,
                               std::uint16_t port)
    : impl_(std::make_unique<Impl>(std::move(destinationAddress), port)) {}

UdpAudioSender::~UdpAudioSender() = default;
UdpAudioSender::UdpAudioSender(UdpAudioSender &&) noexcept = default;
UdpAudioSender &UdpAudioSender::operator=(UdpAudioSender &&) noexcept = default;

void UdpAudioSender::send(std::span<const std::byte> datagram) const {
  impl_->send(datagram);
}

const std::string &UdpAudioSender::destinationAddress() const noexcept {
  return impl_->destinationAddress();
}

std::uint16_t UdpAudioSender::port() const noexcept { return impl_->port(); }
