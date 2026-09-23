#include "DesktopNetworkHost.hpp"
#include "PacketSerializer.hpp"
#include "Room.hpp"
#include "UdpAudioReceiver.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#endif

namespace {
template <typename T> void little(std::ofstream &out, T value) {
  for (std::size_t byte = 0; byte < sizeof(T); ++byte)
    out.put(static_cast<char>((value >> (byte * 8U)) & 0xffU));
}

void testWav(const std::filesystem::path &path) {
  constexpr std::uint32_t frames = 2'000;
  constexpr std::uint32_t bytes = frames * 2;
  std::ofstream out(path, std::ios::binary);
  if (!out) throw std::runtime_error("Cannot create room WAV");
  out.write("RIFF", 4); little(out, 36U + bytes);
  out.write("WAVEfmt ", 8); little(out, 16U);
  little(out, std::uint16_t{1}); little(out, std::uint16_t{1});
  little(out, 8'000U); little(out, 16'000U);
  little(out, std::uint16_t{2}); little(out, std::uint16_t{16});
  out.write("data", 4); little(out, bytes);
  const std::array<char, bytes> silence{};
  out.write(silence.data(), silence.size());
}
}

void scenario(const std::filesystem::path &path, int clientCount,
              std::uint16_t basePort) {
    Room room("0123456789abcdef0123456789abcdef", "Test Room", "host", 42, 7);
    std::vector<std::unique_ptr<UdpAudioReceiver>> receivers;
    std::array<std::atomic<int>, 10> counts{};
    std::atomic_bool done{false};
    std::vector<std::thread> readers;
    for (int index = 0; index < clientCount; ++index) {
      const auto port = static_cast<std::uint16_t>(basePort + index);
      receivers.push_back(std::make_unique<UdpAudioReceiver>(
          "127.0.0.1", port, std::chrono::milliseconds(100)));
      (void)room.join("client-" + std::to_string(index), "127.0.0.1", port);
    }
    if (clientCount == 10)
      (void)room.join("broken", "not-ipv4", 40'600);
    for (int index = 0; index < clientCount; ++index) {
      readers.emplace_back([&, index] {
        while (!done.load(std::memory_order_acquire)) {
          const auto bytes = receivers[index]->receive();
          if (!bytes) continue;
          const auto packet = PacketSerializer::deserialize(*bytes);
          if (packet.sessionId == 42 && packet.streamId == 7)
            counts[index].fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    DesktopNetworkHostConfig config;
    config.port = basePort;
    config.sessionId = 42;
    config.streamId = 7;
    config.room = &room;
    config.sendAhead = std::chrono::milliseconds(100);
    DesktopNetworkHost host(config);
    const auto start = PlaybackClock::now() + std::chrono::milliseconds(100);
    const auto cpuStart = std::clock();
    const auto stats = host.streamFile(path, start, 8'000, 1);
    const auto cpuSeconds = static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    done.store(true, std::memory_order_release);
    for (auto &thread : readers) thread.join();
    if (stats.packetsSent != 25 ||
        stats.clientSendFailures != (clientCount == 10 ? 25U : 0U) ||
        stats.datagramsSent < static_cast<std::uint64_t>(clientCount * 10))
      throw std::runtime_error("Per-client send accounting failed");
    std::uint64_t received = 0;
    for (int index = 0; index < clientCount; ++index) {
      const auto count = counts[index].load(std::memory_order_relaxed);
      if (count == 0)
        throw std::runtime_error("A healthy room client received no audio");
      received += count;
    }
    std::cout << clientCount << " clients: " << stats.datagramsSent
              << " datagrams, " << stats.audioDatagramBytesSent
              << " bytes, " << received << " received, "
              << cpuSeconds << " process CPU-s";
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
      std::cout << ", " << memory.WorkingSetSize / 1'048'576.0
                << " MiB process working set";
#endif
    std::cout << '\n';
}

int main() {
  const auto path = std::filesystem::temp_directory_path() /
                    "tandem_room_transport_test.wav";
  try {
    testWav(path);
    scenario(path, 1, 40'500);
    scenario(path, 2, 40'510);
    scenario(path, 5, 40'520);
    scenario(path, 10, 40'530);
    std::filesystem::remove(path);
    std::cout << "Room scale matrix passed, including one bad endpoint\n";
    return 0;
  } catch (const std::exception &error) {
    std::filesystem::remove(path);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
