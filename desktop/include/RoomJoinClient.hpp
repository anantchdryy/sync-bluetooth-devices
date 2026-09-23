#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct JoinedRoomState {
  std::uint64_t sessionId{};
  std::uint32_t streamId{};
  std::uint16_t audioPort{};
  std::uint16_t clockPort{};
  std::string roomName;
  std::string playbackState{"STOPPED"};
  bool connected{};
};

/// Keeps one room membership alive and polls the host's playback state.
class RoomJoinClient {
public:
  RoomJoinClient(std::string hostAddress, std::uint16_t sessionPort);
  ~RoomJoinClient();
  RoomJoinClient(const RoomJoinClient &) = delete;
  RoomJoinClient &operator=(const RoomJoinClient &) = delete;
  [[nodiscard]] JoinedRoomState snapshot() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
