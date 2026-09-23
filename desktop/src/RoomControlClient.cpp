#include "RoomControlClient.hpp"

#include <stdexcept>
#include <string>

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
}

std::string RoomControlClient::issue(std::uint16_t port,
                                      const std::string &command) {
  if (port == 0 || command.empty() || command.size() > 100 ||
      command.find_first_of("\r\n") != std::string::npos)
    throw std::invalid_argument("Invalid local room command");
#ifdef _WIN32
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    throw std::runtime_error("Control socket startup failed");
#endif
  const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == invalidSocket) {
#ifdef _WIN32
    WSACleanup();
#endif
    throw std::runtime_error("Unable to create local control socket");
  }
  try {
#ifdef _WIN32
    const DWORD timeout = 2'000;
#else
    const timeval timeout{2, 0};
#endif
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char *>(&timeout), sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(socket, reinterpret_cast<const sockaddr *>(&address),
                sizeof(address)) != 0)
      throw std::runtime_error("Local room host is unavailable");
    const auto line = command + "\n";
    std::size_t sent = 0;
    while (sent < line.size()) {
      const auto amount = ::send(socket, line.data() + sent,
                                 static_cast<int>(line.size() - sent), 0);
      if (amount <= 0) throw std::runtime_error("Local room command send failed");
      sent += static_cast<std::size_t>(amount);
    }
    std::string response;
    for (int index = 0; index < 256; ++index) {
      char character{};
      if (recv(socket, &character, 1, 0) != 1)
        throw std::runtime_error("Local room command response timed out");
      if (character == '\n') break;
      response.push_back(character);
    }
    closeSocket(socket);
#ifdef _WIN32
    WSACleanup();
#endif
    return response;
  } catch (...) {
    closeSocket(socket);
#ifdef _WIN32
    WSACleanup();
#endif
    throw;
  }
}
