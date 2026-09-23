#include "Room.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
std::uint64_t random64() {
  std::random_device source;
  std::uint64_t value = 0;
  do {
    value = (static_cast<std::uint64_t>(source()) << 32U) | source();
  } while (value == 0);
  return value;
}
}

Room::Room(std::string roomId, std::string roomName, std::string hostDeviceId,
           std::uint64_t sessionId, std::uint32_t streamId) {
  if (roomId.empty() || roomName.empty() || hostDeviceId.empty() ||
      sessionId == 0 || streamId == 0)
    throw std::invalid_argument("Room identity is incomplete");
  if (roomName.size() > 48 ||
      !std::all_of(roomName.begin(), roomName.end(), [](unsigned char character) {
        return character >= 32 && character <= 126;
      }))
    throw std::invalid_argument("Room name must be 1..48 printable ASCII bytes");
  state_.roomId = std::move(roomId);
  state_.roomName = std::move(roomName);
  state_.hostDeviceId = std::move(hostDeviceId);
  state_.sessionId = sessionId;
  state_.streamId = streamId;
}

Room Room::create(std::string roomName, std::string hostDeviceId) {
  const std::array<std::uint64_t, 2> id{random64(), random64()};
  std::ostringstream text;
  text << std::hex << std::setfill('0') << std::setw(16) << id[0]
       << std::setw(16) << id[1];
  auto streamId = static_cast<std::uint32_t>(random64());
  if (streamId == 0) streamId = 1;
  return Room(text.str(), std::move(roomName), std::move(hostDeviceId),
              random64(), streamId);
}

std::uint64_t Room::join(std::string deviceId, std::string ipv4Address,
                         std::uint16_t audioPort) {
  if (deviceId.empty() || ipv4Address.empty() || audioPort == 0)
    throw std::invalid_argument("Room member endpoint is incomplete");
  std::scoped_lock lock(mutex_);
  const auto token = nextMembershipToken_++;
  members_.insert_or_assign(deviceId, RoomMember{
      deviceId, std::move(ipv4Address), audioPort, token});
  return token;
}

void Room::leave(const std::string &deviceId, std::uint64_t token) {
  std::scoped_lock lock(mutex_);
  const auto iterator = members_.find(deviceId);
  if (iterator != members_.end() && iterator->second.membershipToken == token)
    members_.erase(iterator);
}

bool Room::updateMember(const RoomMember &member) {
  std::scoped_lock lock(mutex_);
  const auto iterator = members_.find(member.deviceId);
  if (iterator == members_.end() ||
      iterator->second.membershipToken != member.membershipToken) return false;
  iterator->second = member;
  return true;
}

void Room::setPlaybackState(RoomPlaybackState state) {
  std::scoped_lock lock(mutex_);
  state_.playbackState = state;
}

RoomSnapshot Room::snapshot() const {
  std::scoped_lock lock(mutex_);
  auto result = state_;
  result.members.reserve(members_.size());
  for (const auto &[id, member] : members_) result.members.push_back(member);
  return result;
}
