#include "ClockSyncTransport.hpp"
#include "DesktopAudioClient.hpp"
#include "DesktopAudioPlayer.hpp"
#include "DesktopNetworkHost.hpp"
#include "ControlChannel.hpp"
#include "DiscoveryService.hpp"
#include "Room.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace {

enum class Mode { Local, Host, Client };

struct CommandLine {
  Mode mode{Mode::Local};
  std::string argument;
  std::string destinationAddress{"255.255.255.255"};
  std::string bindAddress{"0.0.0.0"};
  std::string roomName{"Tandem Audio"};
  std::uint16_t audioPort{40'100};
  std::uint16_t clockSyncPort{40'101};
  std::uint16_t sessionPort{40'102};
  std::chrono::milliseconds outputLatencyAdjustment{};
#ifndef NDEBUG
  std::optional<NetworkImpairmentConfig> impairment;
#endif
};

void printUsage() {
  std::cerr
      << "Usage:\n"
      << "  syncaudio path/to/file.wav\n"
      << "  syncaudio host path/to/file.wav [--address IPv4] [--port PORT] "
         "[--control-port PORT] [--session-port PORT] "
         "[--output-latency-ms -1000..1000] [--room-name NAME]\n"
#ifndef NDEBUG
      << "    Debug host only: [--impair-loss PERCENT] [--impair-delay MS] "
         "[--impair-jitter MS] [--impair-duplicate PERCENT] "
         "[--impair-reorder PERCENT] [--impair-blackout MS] "
         "[--impair-seed INTEGER]\n"
#endif
      << "  syncaudio client <host-ip> [--bind IPv4] [--port PORT] "
         "[--control-port PORT] [--output-latency-ms -1000..1000]\n";
}

#ifndef NDEBUG
double parsePercent(std::string_view text) {
  double value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() ||
      !(value >= 0 && value <= 100)) throw std::invalid_argument("Invalid impairment percent");
  return value;
}

std::uint32_t parseImpairmentInteger(std::string_view text) {
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("Invalid impairment integer");
  return value;
}
#endif

std::uint16_t parsePort(std::string_view text) {
  unsigned int value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value == 0 ||
      value > 65'535) {
    throw std::invalid_argument("Port must be an integer from 1 to 65535");
  }
  return static_cast<std::uint16_t>(value);
}

std::chrono::milliseconds parseOutputLatency(std::string_view text) {
  int value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() ||
      value < -1'000 || value > 1'000)
    throw std::invalid_argument("Output latency must be -1000..1000 ms");
  return std::chrono::milliseconds(value);
}

CommandLine parseCommandLine(int argc, char *argv[]) {
  if (argc == 2) {
    return CommandLine{Mode::Local, argv[1]};
  }
  if (argc < 3) {
    throw std::invalid_argument("Invalid command line");
  }

  CommandLine result;
  const std::string_view mode = argv[1];
  if (mode == "host") {
    result.mode = Mode::Host;
  } else if (mode == "client") {
    result.mode = Mode::Client;
  } else {
    throw std::invalid_argument("Mode must be 'host' or 'client'");
  }
  result.argument = argv[2];

  for (int index = 3; index < argc; ++index) {
    const std::string_view option = argv[index];
    if (option == "--address" && index + 1 < argc &&
        result.mode == Mode::Host) {
      result.destinationAddress = argv[++index];
    } else if (option == "--bind" && index + 1 < argc &&
               result.mode == Mode::Client) {
      result.bindAddress = argv[++index];
    } else if (option == "--port" && index + 1 < argc) {
      result.audioPort = parsePort(argv[++index]);
    } else if (option == "--control-port" && index + 1 < argc) {
      result.clockSyncPort = parsePort(argv[++index]);
    } else if (option == "--session-port" && index + 1 < argc &&
               result.mode == Mode::Host) {
      result.sessionPort = parsePort(argv[++index]);
    } else if (option == "--output-latency-ms" && index + 1 < argc) {
      result.outputLatencyAdjustment = parseOutputLatency(argv[++index]);
    } else if (option == "--room-name" && index + 1 < argc &&
               result.mode == Mode::Host) {
      result.roomName = argv[++index];
#ifndef NDEBUG
    } else if (result.mode == Mode::Host && index + 1 < argc &&
               option.starts_with("--impair-")) {
      if (!result.impairment) result.impairment.emplace();
      const std::string_view value = argv[++index];
      if (option == "--impair-loss") result.impairment->packetLossPercent = parsePercent(value);
      else if (option == "--impair-delay") result.impairment->baseDelayMs = parseImpairmentInteger(value);
      else if (option == "--impair-jitter") result.impairment->jitterMs = parseImpairmentInteger(value);
      else if (option == "--impair-duplicate") result.impairment->duplicatePercent = parsePercent(value);
      else if (option == "--impair-reorder") result.impairment->reorderPercent = parsePercent(value);
      else if (option == "--impair-blackout") result.impairment->blackoutDurationMs = parseImpairmentInteger(value);
      else if (option == "--impair-seed") result.impairment->seed = parseImpairmentInteger(value);
      else throw std::invalid_argument("Unknown impairment option");
#endif
    } else {
      throw std::invalid_argument(
          "Unknown, incomplete, or inapplicable option: " +
          std::string(option));
    }
  }
  if (result.audioPort == result.clockSyncPort ||
      (result.mode == Mode::Host &&
       (result.sessionPort == result.audioPort ||
        result.sessionPort == result.clockSyncPort))) {
    throw std::invalid_argument("Audio, clock, and session ports must differ");
  }
  return result;
}

void printMetadata(const AudioMetadata &metadata) {
  std::cout << "Sample rate: " << metadata.sampleRate << " Hz\n"
            << "Channels:    " << metadata.channels << '\n'
            << "Duration:    " << std::fixed << std::setprecision(2)
            << metadata.durationSeconds << " seconds\n";
}

void waitForPlayback(DesktopAudioPlayer &player) {
  while (player.isPlaying()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  player.stop();
}

void printPlaybackSummary(const DesktopAudioPlayer &player) {
  const auto elapsedSeconds =
      std::chrono::duration<double>(player.elapsedPlaybackTime()).count();
  std::cout << "Playback finished.\n"
            << "Final frame: " << player.currentPlaybackFrame() << '\n'
            << "Elapsed:     " << elapsedSeconds << " seconds\n";

  if (const auto timestamp = player.expectedPlaybackTimestamp()) {
    std::cout << "Expected frame timestamp: "
              << timestamp->time_since_epoch().count()
              << " ns (steady clock)\n";
  }
}

void runLocal(const CommandLine &commandLine) {
  DesktopAudioPlayer player;
  player.load(commandLine.argument);
  printMetadata(player.metadata());
  std::cout << "Playing...\n";
  player.play();
  waitForPlayback(player);
  printPlaybackSummary(player);
}

void runHost(const CommandLine &commandLine) {
  DesktopAudioPlayer player;
  player.load(commandLine.argument);
  printMetadata(player.metadata());

  auto room = Room::create(commandLine.roomName, "desktop-host");
  const auto roomIdentity = room.snapshot();

  DesktopNetworkHostConfig config;
  config.destinationAddress = commandLine.destinationAddress;
  config.port = commandLine.audioPort;
  config.hostOutputLatency = commandLine.outputLatencyAdjustment;
  config.sessionId = roomIdentity.sessionId;
  config.streamId = roomIdentity.streamId;
  config.room = &room;
  ControlStreamState controlState;
  controlState.sessionId = config.sessionId;
  controlState.streamId = config.streamId;
  controlState.room = &room;
  controlState.audioPort = config.port;
  controlState.clockPort = commandLine.clockSyncPort;
  controlState.sampleRate = player.metadata().sampleRate;
  controlState.channels = static_cast<std::uint16_t>(player.metadata().channels);
  config.progressFrame = &controlState.currentFrame;
  config.outputRouteChanged = [&player] { return player.outputRouteChanged(); };
#ifndef NDEBUG
  config.impairment = commandLine.impairment;
#endif
  DesktopNetworkHost host(config);
  ClockSyncServer clockServer("0.0.0.0", commandLine.clockSyncPort);
  std::unique_ptr<DiscoveryService> discovery;
  try {
    discovery = std::make_unique<DiscoveryService>(
        commandLine.sessionPort, roomIdentity.roomName, roomIdentity.roomId);
    controlState.hostAddress = discovery->hostAddress();
  } catch (const std::exception &error) {
    std::cerr << "LAN discovery unavailable: " << error.what() << '\n';
    controlState.hostAddress = "0.0.0.0";
  }
  ControlServer controlServer(commandLine.sessionPort, controlState);

  std::cout << "Room: " << roomIdentity.roomName << " ("
            << roomIdentity.roomId << ")\n"
            << "Per-client UDP port: " << config.port << '\n'
            << "Clock-sync port: " << commandLine.clockSyncPort << '\n'
            << "Session TCP port: " << commandLine.sessionPort << '\n'
            << "Discovered as:    " << controlState.hostAddress << '\n'
            << "PCM format:      signed 16-bit little-endian\n"
            << "Host output latency adjustment: "
            << config.hostOutputLatency.count() << " ms (manual)\n"
            << "Playing locally and streaming...\n";

  const auto scheduledStart = PlaybackClock::now() + config.sendAhead;
  player.playAt(scheduledStart);
  room.setPlaybackState(RoomPlaybackState::Playing);
  controlState.playing.store(true, std::memory_order_release);
  const auto playbackStart = player.expectedPlaybackTimestamp(0);
  if (!playbackStart) {
    throw std::runtime_error("Playback timeline did not start");
  }

  const auto &metadata = player.metadata();
  const auto stats =
      host.streamFile(commandLine.argument, *playbackStart, metadata.sampleRate,
                      static_cast<std::uint16_t>(metadata.channels));
  waitForPlayback(player);
  room.setPlaybackState(RoomPlaybackState::Stopped);
  controlState.playing.store(false, std::memory_order_release);

  std::cout << "Network stream finished.\n"
            << "Session ID:      " << stats.sessionId << '\n'
            << "Packets sent:    " << stats.packetsSent << '\n'
            << "Client datagrams sent: " << stats.datagramsSent << '\n'
            << "Client send failures: " << stats.clientSendFailures << '\n'
            << "Frames sent:     " << stats.framesSent << '\n'
            << "Frames/packet:   " << stats.framesPerPacket << '\n'
            << "Packet duration: " << stats.packetDurationMilliseconds
            << " ms\n"
            << "Audio datagram bytes: " << stats.audioDatagramBytesSent << '\n';
  if (stats.sendDurationSeconds > 0) {
    std::cout << "Audio UDP payload rate: "
              << stats.audioDatagramBytesSent * 8.0 /
                     stats.sendDurationSeconds / 1'000.0
              << " kbps (excludes IP/UDP headers)\n";
  }
  printPlaybackSummary(player);
}

void runClient(const CommandLine &commandLine) {
  DesktopAudioClientConfig config;
  config.bindAddress = commandLine.bindAddress;
  config.audioPort = commandLine.audioPort;
  config.clockSyncPort = commandLine.clockSyncPort;
  config.outputLatencyAdjustment = commandLine.outputLatencyAdjustment;

  std::cout << "Waiting for host " << commandLine.argument << "...\n"
            << "Audio listen port: " << config.audioPort << '\n'
            << "Clock-sync port:   " << config.clockSyncPort << '\n';

  DesktopAudioClient client(config);
  const auto stats = client.run(commandLine.argument);
  std::cout << std::fixed << std::setprecision(3) << "Client stream finished.\n"
            << "Starting frame:            " << stats.startingFrame << '\n'
            << "Final playback frame:      " << stats.finalPlaybackFrame << '\n'
            << "Packets received:          " << stats.packetsReceived << '\n'
            << "Packets lost:              " << stats.packetsLost << '\n'
            << "Out-of-order packets:      " << stats.outOfOrderPackets << '\n'
            << "Late / duplicate packets:  " << stats.latePackets << " / "
            << stats.duplicatePackets << '\n'
            << "Network jitter:            " << stats.networkJitterMilliseconds
            << " ms\n"
            << "Target buffer:             " << stats.targetBufferMilliseconds
            << " ms\n"
            << "Packet loss rate:          " << stats.packetLossPercent
            << "%\n"
            << "Final buffer depth:        "
            << stats.finalBufferDepthMilliseconds << " ms\n"
            << "Peak buffer depth:         "
            << stats.peakBufferDepthMilliseconds << " ms\n"
            << "Host-client clock offset:  " << stats.clockOffsetMilliseconds
            << " ms\n"
            << "Clock-sync round-trip:     " << stats.clockRoundTripMilliseconds
            << " ms\n"
            << "Clock samples / quality:   " << stats.clockSamples << " / "
            << stats.clockMeasurementQuality << '\n'
            << "Estimated drift:           " << stats.estimatedDriftPpm
            << " ppm\n"
            << "Buffer error:              " << stats.bufferErrorMilliseconds
            << " ms\n"
            << "Correction ratio:          " << std::setprecision(6)
            << stats.correctionRatio << std::setprecision(3) << '\n'
            << "Estimated playback delay: "
            << stats.estimatedPlaybackDelayMilliseconds << " ms\n"
            << "Output route:             " << stats.outputRouteName << '\n'
            << "Output latency adjustment: "
            << stats.outputLatencyAdjustmentMilliseconds << " ms\n"
            << "Underrun frames:           " << stats.underrunFrames << '\n';
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    const auto commandLine = parseCommandLine(argc, argv);
    switch (commandLine.mode) {
    case Mode::Local:
      runLocal(commandLine);
      break;
    case Mode::Host:
      runHost(commandLine);
      break;
    case Mode::Client:
      runClient(commandLine);
      break;
    }
    return 0;
  } catch (const std::invalid_argument &error) {
    printUsage();
    std::cerr << "Error: " << error.what() << '\n';
    return 2;
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }
}
