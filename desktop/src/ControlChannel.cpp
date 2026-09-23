#include "ControlChannel.hpp"
#include "Room.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <memory>
#include <cmath>
#include <sstream>
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

std::string responseFor(const std::string &line, ControlStreamState &state,
                        const std::string &peerAddress,
                        std::string &joinedDeviceId,
                        std::uint64_t &membershipToken) {
  if (line == "HELLO 2") return "VERSION 2\n";
  if (line.rfind("HELLO ", 0) == 0) return "ERROR protocol-version\n";
  if (line.rfind("JOIN ", 0) == 0) {
    const auto deviceId = line.substr(5);
    if (!validDeviceId(deviceId)) return "ERROR invalid-device-id\n";
    if (state.room) {
      if (!joinedDeviceId.empty()) state.room->leave(joinedDeviceId, membershipToken);
      membershipToken = state.room->join(deviceId, peerAddress, state.audioPort);
      joinedDeviceId = deviceId;
    }
    std::string response = "WELCOME " + std::to_string(state.sessionId) + " " +
           std::to_string(state.streamId) + " " +
           std::to_string(state.audioPort) + " " +
           std::to_string(state.clockPort) + " " +
           std::to_string(state.sampleRate) + " " +
           std::to_string(state.channels) + " " + state.hostAddress + "\n";
    if (state.room) {
      const auto room = state.room->snapshot();
      const auto syncAt = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch() +
          std::chrono::milliseconds(500)).count();
      response += "ROOM " + room.roomId + " " + room.roomName + "\n";
      response += "SYNC_AT " + std::to_string(syncAt) + "\n";
      response += std::string("HOST_STATE ") +
          (state.playing.load(std::memory_order_acquire) ? "PLAYING " : "STOPPED ") +
          std::to_string(state.sessionId) + " " +
          std::to_string(state.currentFrame.load(std::memory_order_acquire)) + "\n";
    }
    return response;
  }
  if (line == "HOST_STATE") {
    return std::string("HOST_STATE ") +
           (state.playing.load(std::memory_order_acquire) ? "PLAYING " : "STOPPED ") +
           std::to_string(state.sessionId) + " " +
           std::to_string(state.currentFrame.load(std::memory_order_acquire)) + "\n";
  }
  if (line.rfind("CLIENT_STATE ", 0) == 0) {
    if (!state.room) return "OK\n";
    if (joinedDeviceId.empty()) return "ERROR not-joined\n";
    std::istringstream input(line.substr(13));
    std::string status, trailing;
    auto snapshot = state.room->snapshot();
    const auto member = std::find_if(snapshot.members.begin(), snapshot.members.end(),
        [&](const RoomMember &candidate) {
          return candidate.deviceId == joinedDeviceId &&
                 candidate.membershipToken == membershipToken;
        });
    if (member == snapshot.members.end()) return "ERROR not-joined\n";
    auto updated = *member;
    if (!(input >> status >> updated.roundTripMs >> updated.networkJitterMs >>
          updated.packetLossPercent >> updated.bufferDepthMs >>
          updated.estimatedSyncErrorMs >> updated.outputLatencyMs >>
          updated.clockOffsetMs) || input >> trailing)
      return "ERROR invalid-client-state\n";
    for (double value : {updated.roundTripMs, updated.networkJitterMs,
                         updated.packetLossPercent, updated.bufferDepthMs,
                         updated.estimatedSyncErrorMs, updated.outputLatencyMs,
                         updated.clockOffsetMs})
      if (!std::isfinite(value) || std::abs(value) > 1'000'000)
        return "ERROR invalid-client-state\n";
    if (status == "CONNECTED") updated.connectionState = RoomConnectionState::Connected;
    else if (status == "SYNCING") updated.connectionState = RoomConnectionState::Syncing;
    else if (status == "BUFFERING") updated.connectionState = RoomConnectionState::Buffering;
    else if (status == "SYNCED") updated.connectionState = RoomConnectionState::Synced;
    else if (status == "DEGRADED") updated.connectionState = RoomConnectionState::Degraded;
    else if (status == "RECONNECTING") updated.connectionState = RoomConnectionState::Reconnecting;
    else return "ERROR invalid-client-state\n";
    return state.room->updateMember(updated) ? "OK\n" : "ERROR not-joined\n";
  }
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
      std::array<char, INET_ADDRSTRLEN> addressText{};
      if (!inet_ntop(AF_INET, &peer.sin_addr, addressText.data(),
                     addressText.size())) {
        closeSocket(client);
        continue;
      }
      if (clients_.size() >= 32) {
        closeSocket(client);
        continue;
      }
      auto active = std::make_shared<std::atomic_bool>(true);
      clients_.push_back(ClientWorker{
          std::thread(&Impl::serveClient, this, client, active,
                      std::string(addressText.data())), active});
    }
  }

  void serveClient(Socket socket, std::shared_ptr<std::atomic_bool> active,
                   std::string peerAddress) {
    try { receiveTimeout(socket); } catch (...) {
      closeSocket(socket);
      active->store(false, std::memory_order_release);
      return;
    }
    std::string line;
    std::string joinedDeviceId;
    std::uint64_t membershipToken = 0;
    line.reserve(256);
    while (!stop_.load(std::memory_order_acquire)) {
      char character{};
      const auto count = recv(socket, &character, 1, 0);
      if (count == 0) break;
      if (count < 0) continue; // Timed out; inspect stop flag.
      if (character == '\n') {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto response = responseFor(line, state_, peerAddress,
                                          joinedDeviceId, membershipToken);
        if (!sendLine(socket, response) || line == "LEAVE") break;
        line.clear();
      } else if (line.size() < 256 && character >= 32 && character < 127) {
        line.push_back(character);
      } else {
        sendLine(socket, "ERROR malformed-line\n");
        break;
      }
    }
    if (state_.room && !joinedDeviceId.empty())
      state_.room->leave(joinedDeviceId, membershipToken);
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
