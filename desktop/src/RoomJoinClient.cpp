#include "RoomJoinClient.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalidSocket = INVALID_SOCKET;
void closeSocket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket invalidSocket = -1;
void closeSocket(Socket socket) { close(socket); }
#endif

void sendLine(Socket socket, const std::string &line) {
  std::size_t sent = 0;
  while (sent < line.size()) {
    const auto count = send(socket, line.data() + sent,
                            static_cast<int>(line.size() - sent), 0);
    if (count <= 0) throw std::runtime_error("Room control send failed");
    sent += static_cast<std::size_t>(count);
  }
}

std::string readLine(Socket socket) {
  std::string line;
  for (int index = 0; index < 512; ++index) {
    char character{};
    if (recv(socket, &character, 1, 0) != 1)
      throw std::runtime_error("Room control connection was lost");
    if (character == '\n') return line;
    if (character >= 32 && character < 127) line.push_back(character);
    else throw std::runtime_error("Malformed room control response");
  }
  throw std::runtime_error("Room control line exceeded limit");
}
}

class RoomJoinClient::Impl {
public:
  Impl(std::string hostAddress, std::uint16_t sessionPort) {
    if (sessionPort == 0) throw std::invalid_argument("Room control port is invalid");
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
      throw std::runtime_error("Room socket startup failed");
#endif
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == invalidSocket) {
#ifdef _WIN32
      WSACleanup();
#endif
      throw std::runtime_error("Room control socket failed");
    }
    try {
#ifdef _WIN32
      const DWORD timeout = 2'000;
#else
      const timeval timeout{2, 0};
#endif
      setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&timeout), sizeof(timeout));
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_port = htons(sessionPort);
      if (inet_pton(AF_INET, hostAddress.c_str(), &address.sin_addr) != 1)
        throw std::invalid_argument("Room host must be an IPv4 address");
      if (connect(socket_, reinterpret_cast<const sockaddr *>(&address),
                  sizeof(address)) != 0)
        throw std::runtime_error("Room host is unavailable");
      sendLine(socket_, "HELLO 2\n");
      if (readLine(socket_) != "VERSION 2")
        throw std::runtime_error("Room protocol version differs");
      sendLine(socket_, "JOIN desktop-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) + "\n");
      std::istringstream welcome(readLine(socket_));
      std::string keyword, host;
      unsigned int audioPort = 0, clockPort = 0, sampleRate = 0, channels = 0;
      if (!(welcome >> keyword >> state_.sessionId >> state_.streamId >>
            audioPort >> clockPort >> sampleRate >> channels >> host) ||
          keyword != "WELCOME" || state_.sessionId == 0 || state_.streamId == 0 ||
          audioPort == 0 || audioPort > 65'535 ||
          clockPort == 0 || clockPort > 65'535)
        throw std::runtime_error("Room welcome was invalid");
      state_.audioPort = static_cast<std::uint16_t>(audioPort);
      state_.clockPort = static_cast<std::uint16_t>(clockPort);
      const auto roomLine = readLine(socket_);
      if (roomLine.rfind("ROOM ", 0) != 0)
        throw std::runtime_error("Room metadata was missing");
      const auto separator = roomLine.find(' ', 5);
      if (separator != std::string::npos) state_.roomName = roomLine.substr(separator + 1);
      if (readLine(socket_).rfind("SYNC_AT ", 0) != 0)
        throw std::runtime_error("Room sync point was missing");
      updateState(readLine(socket_));
      state_.connected = true;
      thread_ = std::thread(&Impl::poll, this);
    } catch (...) {
      closeSocket(socket_);
#ifdef _WIN32
      WSACleanup();
#endif
      throw;
    }
  }

  ~Impl() {
    stop_.store(true, std::memory_order_release);
#ifdef _WIN32
    shutdown(socket_, SD_BOTH);
#else
    shutdown(socket_, SHUT_RDWR);
#endif
    if (thread_.joinable()) thread_.join();
    closeSocket(socket_);
#ifdef _WIN32
    WSACleanup();
#endif
  }

  JoinedRoomState snapshot() const {
    std::scoped_lock lock(mutex_);
    return state_;
  }

private:
  void updateState(const std::string &line) {
    std::istringstream input(line);
    std::string keyword, playback, token;
    std::uint64_t session = 0, frame = 0;
    if (!(input >> keyword >> playback >> session >> frame) ||
        keyword != "HOST_STATE" || session != state_.sessionId)
      throw std::runtime_error("Room session changed");
    std::uint32_t stream = state_.streamId;
    while (input >> token) {
      if (token == "STREAM" && !(input >> stream))
        throw std::runtime_error("Room stream ID was invalid");
    }
    std::scoped_lock lock(mutex_);
    state_.playbackState = playback;
    state_.streamId = stream;
  }

  void poll() noexcept {
    try {
      while (!stop_.load(std::memory_order_acquire)) {
        sendLine(socket_, "HOST_STATE\n");
        updateState(readLine(socket_));
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }
    } catch (...) {
      std::scoped_lock lock(mutex_);
      state_.connected = false;
    }
  }

  Socket socket_{invalidSocket};
  mutable std::mutex mutex_;
  JoinedRoomState state_;
  std::atomic_bool stop_{false};
  std::thread thread_;
};

RoomJoinClient::RoomJoinClient(std::string hostAddress, std::uint16_t sessionPort)
    : impl_(std::make_unique<Impl>(std::move(hostAddress), sessionPort)) {}
RoomJoinClient::~RoomJoinClient() = default;
JoinedRoomState RoomJoinClient::snapshot() const { return impl_->snapshot(); }
