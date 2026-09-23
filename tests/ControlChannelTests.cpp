#include "ControlChannel.hpp"

#include <array>
#include <stdexcept>
#include <string>

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

std::string exchange(Socket socket, const std::string &request) {
  require(send(socket, request.data(), static_cast<int>(request.size()), 0) ==
              static_cast<int>(request.size()), "Control send failed");
  std::string response;
  for (int index = 0; index < 256; ++index) {
    char byte{};
    require(recv(socket, &byte, 1, 0) == 1, "Control response failed");
    response.push_back(byte);
    if (byte == '\n') return response;
  }
  throw std::runtime_error("Control response was unbounded");
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
}
