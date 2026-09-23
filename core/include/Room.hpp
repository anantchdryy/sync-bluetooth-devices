#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

enum class RoomPlaybackState { Stopped, Playing, Paused };
enum class RoomConnectionState {
  Connected, Syncing, Buffering, Synced, Degraded, Reconnecting
};

struct RoomMember {
  std::string deviceId;
  std::string ipv4Address;
  std::uint16_t audioPort{};
  std::uint64_t membershipToken{};
  RoomConnectionState connectionState{RoomConnectionState::Connected};
  double roundTripMs{};
  double clockOffsetMs{};
  double networkJitterMs{};
  double packetLossPercent{};
  double bufferDepthMs{};
  double outputLatencyMs{};
  double estimatedSyncErrorMs{};
};

struct RoomSnapshot {
  std::string roomId;
  std::string roomName;
  std::string hostDeviceId;
  std::uint64_t sessionId{};
  std::uint32_t streamId{};
  RoomPlaybackState playbackState{RoomPlaybackState::Stopped};
  std::vector<RoomMember> members;
};

class Room {
public:
  Room(std::string roomId, std::string roomName, std::string hostDeviceId,
       std::uint64_t sessionId, std::uint32_t streamId);
  [[nodiscard]] static Room create(std::string roomName,
                                   std::string hostDeviceId);
  [[nodiscard]] std::uint64_t join(std::string deviceId,
                                   std::string ipv4Address,
                                   std::uint16_t audioPort);
  void leave(const std::string &deviceId, std::uint64_t membershipToken);
  [[nodiscard]] bool updateMember(const RoomMember &member);
  void setPlaybackState(RoomPlaybackState state);
  [[nodiscard]] RoomSnapshot snapshot() const;

private:
  mutable std::mutex mutex_;
  RoomSnapshot state_;
  std::unordered_map<std::string, RoomMember> members_;
  std::uint64_t nextMembershipToken_{1};
};
