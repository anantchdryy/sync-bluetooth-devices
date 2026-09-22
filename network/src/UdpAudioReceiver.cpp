#include "UdpAudioReceiver.hpp"

#include "PacketSerializer.hpp"

#include <array>
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
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle invalidSocket = INVALID_SOCKET;

std::string socketError() {
  return "Winsock error " + std::to_string(WSAGetLastError());
}

bool isTimeoutError() {
  const auto error = WSAGetLastError();
  return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
}

void closeSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle invalidSocket = -1;

std::string socketError() { return std::strerror(errno); }

bool isTimeoutError() { return errno == EAGAIN || errno == EWOULDBLOCK; }

void closeSocket(SocketHandle socket) { close(socket); }
#endif

} // namespace

class UdpAudioReceiver::Impl {
public:
  Impl(std::string bindAddress, std::uint16_t port,
       std::chrono::milliseconds timeout) {
    if (port == 0) {
      throw std::invalid_argument("UDP receive port must be non-zero");
    }
    if (timeout <= std::chrono::milliseconds::zero()) {
      throw std::invalid_argument("UDP receive timeout must be positive");
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
      throw std::runtime_error("Unable to create UDP receive socket: " + error);
    }

#ifdef _WIN32
    const auto timeoutValue = static_cast<DWORD>(timeout.count());
#else
    const timeval timeoutValue{
        static_cast<time_t>(timeout.count() / 1'000),
        static_cast<suseconds_t>((timeout.count() % 1'000) * 1'000)};
#endif
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char *>(&timeoutValue),
                   sizeof(timeoutValue)) != 0) {
      const auto error = socketError();
      cleanup();
      throw std::runtime_error("Unable to set UDP receive timeout: " + error);
    }

    sockaddr_in localAddress{};
    localAddress.sin_family = AF_INET;
    localAddress.sin_port = htons(port);
    if (inet_pton(AF_INET, bindAddress.c_str(), &localAddress.sin_addr) != 1) {
      cleanup();
      throw std::invalid_argument(
          "UDP bind address must be a numeric IPv4 address");
    }
    if (bind(socket_, reinterpret_cast<const sockaddr *>(&localAddress),
#ifdef _WIN32
             static_cast<int>(sizeof(localAddress))) != 0) {
#else
             static_cast<socklen_t>(sizeof(localAddress))) != 0) {
#endif
      const auto error = socketError();
      cleanup();
      throw std::runtime_error("Unable to bind UDP receive socket: " + error);
    }
  }

  ~Impl() { cleanup(); }

  [[nodiscard]] std::optional<std::vector<std::byte>> receive() const {
    std::array<std::byte, PacketSerializer::MaximumDatagramSize + 1> buffer{};
    sockaddr_in sender{};
#ifdef _WIN32
    int senderSize = sizeof(sender);
    const auto received =
        recvfrom(socket_, reinterpret_cast<char *>(buffer.data()),
                 static_cast<int>(buffer.size()), 0,
                 reinterpret_cast<sockaddr *>(&sender), &senderSize);
    if (received == SOCKET_ERROR) {
#else
    socklen_t senderSize = sizeof(sender);
    const auto received =
        recvfrom(socket_, buffer.data(), buffer.size(), 0,
                 reinterpret_cast<sockaddr *>(&sender), &senderSize);
    if (received < 0) {
#endif
      if (isTimeoutError()) {
        return std::nullopt;
      }
      throw std::runtime_error("Unable to receive UDP audio packet: " +
                               socketError());
    }
    if (received == 0 || static_cast<std::size_t>(received) >
                             PacketSerializer::MaximumDatagramSize) {
      throw std::runtime_error("Received UDP audio datagram has invalid size");
    }
    return std::vector<std::byte>(buffer.begin(), buffer.begin() + received);
  }

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

  SocketHandle socket_{invalidSocket};
#ifdef _WIN32
  bool winsockInitialized_{false};
#endif
};

UdpAudioReceiver::UdpAudioReceiver(std::string bindAddress, std::uint16_t port,
                                   std::chrono::milliseconds timeout)
    : impl_(std::make_unique<Impl>(std::move(bindAddress), port, timeout)) {}

UdpAudioReceiver::~UdpAudioReceiver() = default;
UdpAudioReceiver::UdpAudioReceiver(UdpAudioReceiver &&) noexcept = default;
UdpAudioReceiver &
UdpAudioReceiver::operator=(UdpAudioReceiver &&) noexcept = default;

std::optional<std::vector<std::byte>> UdpAudioReceiver::receive() const {
  return impl_->receive();
}
