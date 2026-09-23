#include "Room.hpp"

#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
}

int main() {
  try {
    auto room = Room::create("Living Room", "windows-host");
    const auto identity = room.snapshot();
    require(identity.roomId.size() == 32 && identity.sessionId != 0 &&
                identity.streamId != 0, "Room IDs are invalid");
    std::vector<std::thread> joiners;
    for (int index = 0; index < 10; ++index) {
      joiners.emplace_back([&room, index] {
        (void)room.join("client-" + std::to_string(index),
                  "192.168.1." + std::to_string(index + 10), 40100);
      });
    }
    for (auto &thread : joiners) thread.join();
    require(room.snapshot().members.size() == 10, "Concurrent joins lost members");
    auto first = room.snapshot().members.front();
    room.leave(first.deviceId, first.membershipToken + 1);
    require(room.snapshot().members.size() == 10, "Stale leave removed a member");
    first.connectionState = RoomConnectionState::Degraded;
    first.packetLossPercent = 10;
    require(room.updateMember(first), "Member health update failed");
    room.leave(first.deviceId, first.membershipToken);
    require(room.snapshot().members.size() == 9, "Member leave failed");
    room.setPlaybackState(RoomPlaybackState::Playing);
    require(room.snapshot().playbackState == RoomPlaybackState::Playing,
            "Room playback state was not stored");
    require(room.scheduleAction({RoomAction::Pause, 123'000, 0,
                                 identity.streamId}),
            "Future room command was not scheduled");
    require(!room.scheduleAction({RoomAction::Stop, 124'000, 0,
                                  identity.streamId}),
            "Second room command replaced a pending command");
    const auto command = room.takeAction();
    require(command && command->action == RoomAction::Pause &&
                command->effectiveHostNanoseconds == 123'000,
            "Scheduled room command was corrupted");
    std::cout << "Room joins, independent status, and leave tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
