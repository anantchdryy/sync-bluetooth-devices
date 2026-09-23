#include "ControlChannel.hpp"
#include "Room.hpp"
#include "RoomControlClient.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
using Socket = SOCKET;
void closeSocket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
void closeSocket(Socket socket) { close(socket); }
#endif

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

std::string readLine(Socket socket) {
  std::string response;
  for (int index = 0; index < 256; ++index) {
    char byte{};
    require(recv(socket, &byte, 1, 0) == 1, "Control response failed");
    response.push_back(byte);
    if (byte == '\n') return response;
  }
  throw std::runtime_error("Control response was unbounded");
}

std::string exchange(Socket socket, const std::string &request) {
  require(send(socket, request.data(), static_cast<int>(request.size()), 0) ==
              static_cast<int>(request.size()), "Control send failed");
  return readLine(socket);
}
} // namespace

int main() {
  ControlStreamState state;
  state.sessionId = 42;
  state.streamId = 1;
  state.sampleRate = 48'000;
  state.channels = 2;
  state.hostAddress = "127.0.0.1";
  state.currentFrame.store(960);
  state.playing.store(true);
  ControlServer server(0, state);
  const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
  require(socket != INVALID_SOCKET, "Control client socket failed");
#else
  require(socket >= 0, "Control client socket failed");
#endif
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(server.port());
  require(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1,
          "Loopback address failed");
  require(connect(socket, reinterpret_cast<const sockaddr *>(&address),
                  sizeof(address)) == 0, "Control connect failed");
  require(exchange(socket, "JOIN test-device\n") ==
              "WELCOME 42 1 40100 40101 48000 2 127.0.0.1\n",
          "JOIN metadata failed");
  require(exchange(socket, "HOST_STATE\n") == "HOST_STATE PLAYING 42 960\n",
          "HOST_STATE failed");
  require(exchange(socket, "PLAY\n") == "ERROR unsupported-command\n",
          "Unsupported command was not rejected");
  require(exchange(socket, "LEAVE\n") == "BYE\n", "LEAVE failed");
  closeSocket(socket);
  for (int attempt = 0; attempt < 40; ++attempt) {
    const auto reconnect = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    require(connect(reconnect, reinterpret_cast<const sockaddr *>(&address),
                    sizeof(address)) == 0, "Repeated control connect failed");
    require(exchange(reconnect, "JOIN test-device\n").starts_with("WELCOME "),
            "Reconnect was not admitted");
    require(exchange(reconnect, "LEAVE\n") == "BYE\n", "Reconnect leave failed");
    closeSocket(reconnect);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  Room room("room-123", "Living Room", "desktop", 42, 7);
  ControlStreamState roomState;
  roomState.sessionId = 42;
  roomState.streamId = 7;
  roomState.sampleRate = 48'000;
  roomState.channels = 2;
  roomState.hostAddress = "127.0.0.1";
  roomState.room = &room;
  ControlServer roomServer(0, roomState);
  address.sin_port = htons(roomServer.port());
  const auto first = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  const auto second = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  require(connect(first, reinterpret_cast<const sockaddr *>(&address),
                  sizeof(address)) == 0, "First room join failed");
  require(connect(second, reinterpret_cast<const sockaddr *>(&address),
                  sizeof(address)) == 0, "Second room join failed");
  require(exchange(first, "HELLO 2\n") == "VERSION 2\n", "Version negotiation failed");
  require(exchange(first, "JOIN first\n") ==
              "WELCOME 42 7 40100 40101 48000 2 127.0.0.1\n",
          "Room welcome failed");
  require(readLine(first) == "ROOM room-123 Living Room\n", "Room metadata failed");
  require(readLine(first).starts_with("SYNC_AT "), "Future sync point missing");
  require(readLine(first).starts_with("HOST_STATE "), "Join state missing");
  require(exchange(second, "JOIN second\n").starts_with("WELCOME "),
          "Second client was not admitted");
  for (int line = 0; line < 3; ++line) (void)readLine(second);
  require(room.snapshot().members.size() == 2, "Concurrent room members missing");
  require(exchange(first, "MEMBERS\n").starts_with("MEMBER "),
          "Host member query failed");
  require(readLine(first).starts_with("MEMBER "), "Second member was not listed");
  require(readLine(first) == "END\n", "Member list was not terminated");
  require(exchange(first, "CLIENT_STATE SYNCED 2 1 0 180 3 35 20\n") == "OK\n",
          "Client health report failed");
  require(exchange(first, "PLAY\n").starts_with("SCHEDULED PLAY "),
          "Local host control was not scheduled");
  require(room.snapshot().pendingAction.has_value(),
          "Host control action was not queued");
  (void)room.takeAction();
  require(RoomControlClient::issue(roomServer.port(), "PLAY").starts_with("SCHEDULED PLAY "),
          "Local CLI control client failed");
  (void)room.takeAction();
  require(exchange(first, "LEAVE\n") == "BYE\n", "Room leave failed");
  closeSocket(first);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  require(room.snapshot().members.size() == 1, "Leaving client remains in room");
  require(exchange(second, "LEAVE\n") == "BYE\n", "Second room leave failed");
  closeSocket(second);
}
