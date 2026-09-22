#include "ClockSync.hpp"

#include "PlaybackClock.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

std::uint64_t nowNanoseconds() {
  const auto count = PlaybackClock::now().time_since_epoch().count();
  if (count < 0) {
    throw std::runtime_error("Monotonic clock returned a negative timestamp");
  }
  return static_cast<std::uint64_t>(count);
}

std::int64_t checkedTimestamp(std::uint64_t value) {
  if (value >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    throw std::runtime_error("Clock timestamp exceeds the supported range");
  }
  return static_cast<std::int64_t>(value);
}

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle invalidSocket = INVALID_SOCKET;

std::string socketError() {
  return "Winsock error " + std::to_string(WSAGetLastError());
}

bool isTimeoutError() {
  const auto error = WSAGetLastError();
  // Windows reports ICMP "port unreachable" as WSAECONNRESET for UDP. Treat
  // it like a missed probe so a client can be started before the host.
  return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK ||
         error == WSAECONNRESET;
}

void closeSocket(SocketHandle socket) { closesocket(socket); }
#else
using SocketHandle = int;
constexpr SocketHandle invalidSocket = -1;

std::string socketError() { return std::strerror(errno); }

bool isTimeoutError() { return errno == EAGAIN || errno == EWOULDBLOCK; }

void closeSocket(SocketHandle socket) { close(socket); }
#endif

class SocketRuntime {
public:
  SocketRuntime() {
#ifdef _WIN32
    WSADATA winsockData{};
    const auto result = WSAStartup(MAKEWORD(2, 2), &winsockData);
    if (result != 0) {
      throw std::runtime_error("Unable to initialize Winsock: error " +
                               std::to_string(result));
    }
#endif
  }

  ~SocketRuntime() {
#ifdef _WIN32
    WSACleanup();
#endif
  }
};

void setReceiveTimeout(SocketHandle socket, std::chrono::milliseconds timeout) {
#ifdef _WIN32
  const auto value = static_cast<DWORD>(timeout.count());
#else
  const timeval value{
      static_cast<time_t>(timeout.count() / 1'000),
      static_cast<suseconds_t>((timeout.count() % 1'000) * 1'000)};
#endif
  if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&value), sizeof(value)) != 0) {
    throw std::runtime_error("Unable to set clock socket timeout: " +
                             socketError());
  }
}

sockaddr_in makeAddress(const std::string &address, std::uint16_t port) {
  sockaddr_in result{};
  result.sin_family = AF_INET;
  result.sin_port = htons(port);
  if (inet_pton(AF_INET, address.c_str(), &result.sin_addr) != 1) {
    throw std::invalid_argument("Clock-sync address must be numeric IPv4");
  }
  return result;
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

class ClockSyncServer::Impl {
public:
  Impl(const std::string &bindAddress, std::uint16_t port)
      : localAddress_(makeAddress(bindAddress, port)) {
    if (port == 0) {
      throw std::invalid_argument("Clock-sync port must be non-zero");
    }
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == invalidSocket) {
      throw std::runtime_error("Unable to create clock-sync socket: " +
                               socketError());
    }
    try {
      setReceiveTimeout(socket_, std::chrono::milliseconds{100});
      if (bind(socket_, reinterpret_cast<const sockaddr *>(&localAddress_),
#ifdef _WIN32
               static_cast<int>(sizeof(localAddress_))) != 0) {
#else
               static_cast<socklen_t>(sizeof(localAddress_))) != 0) {
#endif
        throw std::runtime_error("Unable to bind clock-sync socket: " +
                                 socketError());
      }
      thread_ = std::thread(&Impl::run, this);
    } catch (...) {
      closeSocket(socket_);
      socket_ = invalidSocket;
      throw;
    }
  }

  ~Impl() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
      thread_.join();
    }
    if (socket_ != invalidSocket) {
      closeSocket(socket_);
    }
  }

private:
  void run() noexcept {
    std::array<std::byte, ClockSyncSerializer::MessageSize> buffer{};
    while (!stop_.load(std::memory_order_acquire)) {
      sockaddr_in clientAddress{};
#ifdef _WIN32
      int clientSize = sizeof(clientAddress);
      const auto received =
          recvfrom(socket_, reinterpret_cast<char *>(buffer.data()),
                   static_cast<int>(buffer.size()), 0,
                   reinterpret_cast<sockaddr *>(&clientAddress), &clientSize);
      if (received == SOCKET_ERROR) {
#else
      socklen_t clientSize = sizeof(clientAddress);
      const auto received =
          recvfrom(socket_, buffer.data(), buffer.size(), 0,
                   reinterpret_cast<sockaddr *>(&clientAddress), &clientSize);
      if (received < 0) {
#endif
        if (isTimeoutError()) {
          continue;
        }
        return;
      }

      const auto t2 = nowNanoseconds();
      try {
        auto message =
            ClockSyncSerializer::deserialize(std::span<const std::byte>(
                buffer.data(), static_cast<std::size_t>(received)));
        if (message.type != ClockSyncMessageType::Request) {
          continue;
        }
        message.type = ClockSyncMessageType::Response;
        message.hostReceiveTimestampNanoseconds = t2;
        message.hostSendTimestampNanoseconds = nowNanoseconds();
        const auto response = ClockSyncSerializer::serialize(message);
        sendto(socket_, reinterpret_cast<const char *>(response.data()),
#ifdef _WIN32
               static_cast<int>(response.size()),
#else
               response.size(),
#endif
               0, reinterpret_cast<const sockaddr *>(&clientAddress),
               clientSize);
      } catch (...) {
        // Ignore malformed control datagrams and keep serving valid clients.
      }
    }
  }

  SocketRuntime runtime_;
  sockaddr_in localAddress_{};
  SocketHandle socket_{invalidSocket};
  std::atomic_bool stop_{false};
  std::thread thread_;
};

ClockSyncServer::ClockSyncServer(std::string bindAddress, std::uint16_t port)
    : impl_(std::make_unique<Impl>(bindAddress, port)) {}

ClockSyncServer::~ClockSyncServer() = default;

ClockSyncEstimate ClockSyncClient::measure(const std::string &hostAddress,
                                           std::uint16_t port,
                                           std::chrono::seconds overallTimeout,
                                           std::uint32_t desiredSamples) {
  if (port == 0 || desiredSamples == 0 ||
      overallTimeout <= std::chrono::seconds::zero()) {
    throw std::invalid_argument("Clock-sync measurement settings are invalid");
  }

  SocketRuntime runtime;
  const auto socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socketHandle == invalidSocket) {
    throw std::runtime_error("Unable to create clock-sync client socket: " +
                             socketError());
  }

  struct SocketGuard {
    SocketHandle socket;
    ~SocketGuard() { closeSocket(socket); }
  } guard{socketHandle};

  setReceiveTimeout(socketHandle, std::chrono::milliseconds{200});
  const auto destination = makeAddress(hostAddress, port);
  const auto deadline = PlaybackClock::now() + overallTimeout;

  struct Sample {
    std::chrono::nanoseconds offset;
    std::chrono::nanoseconds roundTrip;
  };
  std::vector<Sample> samples;
  std::uint32_t requestId = 1;

  while (samples.size() < desiredSamples && PlaybackClock::now() < deadline) {
    ClockSyncMessage request;
    request.requestId = requestId++;
    request.clientSendTimestampNanoseconds = nowNanoseconds();
    const auto encoded = ClockSyncSerializer::serialize(request);
    const auto sent =
        sendto(socketHandle, reinterpret_cast<const char *>(encoded.data()),
#ifdef _WIN32
               static_cast<int>(encoded.size()),
#else
               encoded.size(),
#endif
               0, reinterpret_cast<const sockaddr *>(&destination),
#ifdef _WIN32
               static_cast<int>(sizeof(destination)));
#else
               static_cast<socklen_t>(sizeof(destination)));
#endif
    if (sent < 0) {
      throw std::runtime_error("Unable to send clock-sync request: " +
                               socketError());
    }

    std::array<std::byte, ClockSyncSerializer::MessageSize> responseBytes{};
    sockaddr_in sender{};
#ifdef _WIN32
    int senderSize = sizeof(sender);
    const auto received =
        recvfrom(socketHandle, reinterpret_cast<char *>(responseBytes.data()),
                 static_cast<int>(responseBytes.size()), 0,
                 reinterpret_cast<sockaddr *>(&sender), &senderSize);
    if (received == SOCKET_ERROR) {
#else
    socklen_t senderSize = sizeof(sender);
    const auto received =
        recvfrom(socketHandle, responseBytes.data(), responseBytes.size(), 0,
                 reinterpret_cast<sockaddr *>(&sender), &senderSize);
    if (received < 0) {
#endif
      if (isTimeoutError()) {
        continue;
      }
      throw std::runtime_error("Unable to receive clock-sync response: " +
                               socketError());
    }
    const auto t4 = checkedTimestamp(nowNanoseconds());

    try {
      const auto response =
          ClockSyncSerializer::deserialize(std::span<const std::byte>(
              responseBytes.data(), static_cast<std::size_t>(received)));
      if (response.type != ClockSyncMessageType::Response ||
          response.requestId != request.requestId ||
          response.clientSendTimestampNanoseconds !=
              request.clientSendTimestampNanoseconds) {
        continue;
      }
      const auto t1 = checkedTimestamp(response.clientSendTimestampNanoseconds);
      const auto t2 =
          checkedTimestamp(response.hostReceiveTimestampNanoseconds);
      const auto t3 = checkedTimestamp(response.hostSendTimestampNanoseconds);
      const auto roundTrip = (t4 - t1) - (t3 - t2);
      const auto offset = static_cast<std::int64_t>(
          std::llround((static_cast<long double>(t2 - t1) +
                        static_cast<long double>(t3 - t4)) /
                       2.0L));
      if (roundTrip >= 0) {
        samples.push_back(Sample{std::chrono::nanoseconds{offset},
                                 std::chrono::nanoseconds{roundTrip}});
      }
    } catch (const std::invalid_argument &) {
      continue;
    }
  }

  if (samples.empty()) {
    throw std::runtime_error(
        "Clock sync timed out; ensure the host and control port are reachable");
  }
  const auto best =
      std::min_element(samples.begin(), samples.end(),
                       [](const Sample &left, const Sample &right) {
                         return left.roundTrip < right.roundTrip;
                       });
  return ClockSyncEstimate{best->offset, best->roundTrip,
                           static_cast<std::uint32_t>(samples.size())};
}
