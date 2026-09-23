#include "ControlChannel.hpp"

#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
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

void receiveTimeout(Socket socket) {
#ifdef _WIN32
  const DWORD timeout = 200;
#else
  const timeval timeout{0, 200'000};
#endif
  if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&timeout), sizeof(timeout)) != 0) {
    throw std::runtime_error("Unable to configure control socket timeout");
  }
}

bool sendLine(Socket socket, const std::string &line) {
  std::size_t sent = 0;
  while (sent < line.size()) {
    const auto result = send(socket, line.data() + sent,
                            static_cast<int>(line.size() - sent), 0);
    if (result <= 0) return false;
    sent += static_cast<std::size_t>(result);
  }
  return true;
}

bool validDeviceId(const std::string &id) {
  if (id.empty() || id.size() > 64) return false;
  for (const auto character : id) {
    if (!std::isalnum(static_cast<unsigned char>(character)) &&
        character != '-' && character != '_') return false;
  }
  return true;
}

std::string responseFor(const std::string &line, ControlStreamState &state) {
  if (line.rfind("JOIN ", 0) == 0) {
    if (!validDeviceId(line.substr(5))) return "ERROR invalid-device-id\n";
    return "WELCOME " + std::to_string(state.sessionId) + " " +
           std::to_string(state.streamId) + " " +
           std::to_string(state.audioPort) + " " +
           std::to_string(state.clockPort) + " " +
           std::to_string(state.sampleRate) + " " +
           std::to_string(state.channels) + " " + state.hostAddress + "\n";
  }
  if (line == "HOST_STATE") {
    return std::string("HOST_STATE ") +
           (state.playing.load(std::memory_order_acquire) ? "PLAYING " : "STOPPED ") +
           std::to_string(state.sessionId) + " " +
           std::to_string(state.currentFrame.load(std::memory_order_acquire)) + "\n";
  }
  if (line.rfind("CLIENT_STATE ", 0) == 0) return "OK\n";
  if (line == "LEAVE") return "BYE\n";
  if (line == "PLAY" || line == "PAUSE" || line.rfind("SEEK ", 0) == 0)
    return "ERROR unsupported-command\n";
  return "ERROR unknown-command\n";
}
} // namespace

class ControlServer::Impl {
public:
  Impl(std::uint16_t port, ControlStreamState &state) : state_(state) {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
      throw std::runtime_error("Winsock startup failed for control channel");
#endif
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == invalidSocket) throw std::runtime_error("Control socket failed");
    try {
      int reuse = 1;
      setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&reuse), sizeof(reuse));
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_ANY);
      address.sin_port = htons(port);
      if (bind(listener_, reinterpret_cast<const sockaddr *>(&address),
               sizeof(address)) != 0 || listen(listener_, 16) != 0)
        throw std::runtime_error("Unable to bind control TCP port");
      sockaddr_in bound{};
#ifdef _WIN32
      int boundSize = sizeof(bound);
#else
      socklen_t boundSize = sizeof(bound);
#endif
      if (getsockname(listener_, reinterpret_cast<sockaddr *>(&bound),
                      &boundSize) != 0)
        throw std::runtime_error("Unable to inspect control TCP port");
      boundPort_ = ntohs(bound.sin_port);
      acceptThread_ = std::thread(&Impl::acceptLoop, this);
    } catch (...) {
      closeSocket(listener_);
      listener_ = invalidSocket;
#ifdef _WIN32
      WSACleanup();
#endif
      throw;
    }
  }

  ~Impl() {
    stop_.store(true, std::memory_order_release);
    if (acceptThread_.joinable()) acceptThread_.join();
    for (auto &client : clients_) if (client.thread.joinable()) client.thread.join();
    closeSocket(listener_);
#ifdef _WIN32
    WSACleanup();
#endif
  }

  std::uint16_t port() const noexcept { return boundPort_; }

private:
  void acceptLoop() {
    while (!stop_.load(std::memory_order_acquire)) {
      for (auto iterator = clients_.begin(); iterator != clients_.end();) {
        if (!iterator->active->load(std::memory_order_acquire)) {
          if (iterator->thread.joinable()) iterator->thread.join();
          iterator = clients_.erase(iterator);
        } else ++iterator;
      }
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(listener_, &readable);
      timeval timeout{0, 200'000};
#ifdef _WIN32
      const auto selected = select(0, &readable, nullptr, nullptr, &timeout);
#else
      const auto selected = select(listener_ + 1, &readable, nullptr, nullptr, &timeout);
#endif
      if (selected <= 0) continue;
      sockaddr_in peer{};
#ifdef _WIN32
      int length = sizeof(peer);
#else
      socklen_t length = sizeof(peer);
#endif
      const auto client = accept(listener_, reinterpret_cast<sockaddr *>(&peer), &length);
      if (client == invalidSocket) continue;
      if (clients_.size() >= 32) {
        closeSocket(client);
        continue;
      }
      auto active = std::make_shared<std::atomic_bool>(true);
      clients_.push_back(ClientWorker{
          std::thread(&Impl::serveClient, this, client, active), active});
    }
  }

  void serveClient(Socket socket, std::shared_ptr<std::atomic_bool> active) {
    try { receiveTimeout(socket); } catch (...) {
      closeSocket(socket);
      active->store(false, std::memory_order_release);
      return;
    }
    std::string line;
    line.reserve(256);
    while (!stop_.load(std::memory_order_acquire)) {
      char character{};
      const auto count = recv(socket, &character, 1, 0);
      if (count == 0) break;
      if (count < 0) continue; // Timed out; inspect stop flag.
      if (character == '\n') {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto response = responseFor(line, state_);
        if (!sendLine(socket, response) || line == "LEAVE") break;
        line.clear();
      } else if (line.size() < 256 && character >= 32 && character < 127) {
        line.push_back(character);
      } else {
        sendLine(socket, "ERROR malformed-line\n");
        break;
      }
    }
    closeSocket(socket);
    active->store(false, std::memory_order_release);
  }

  struct ClientWorker {
    std::thread thread;
    std::shared_ptr<std::atomic_bool> active;
  };

  ControlStreamState &state_;
  Socket listener_{invalidSocket};
  std::uint16_t boundPort_{};
  std::atomic_bool stop_{false};
  std::thread acceptThread_;
  std::vector<ClientWorker> clients_;
};

ControlServer::ControlServer(std::uint16_t port, ControlStreamState &state)
    : impl_(std::make_unique<Impl>(port, state)) {}
ControlServer::~ControlServer() = default;
std::uint16_t ControlServer::port() const noexcept { return impl_->port(); }
