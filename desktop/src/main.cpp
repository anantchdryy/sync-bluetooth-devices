#include "ClockSync.hpp"
#include "DesktopAudioClient.hpp"
#include "DesktopAudioPlayer.hpp"
#include "DesktopNetworkHost.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
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
  std::uint16_t audioPort{40'100};
  std::uint16_t clockSyncPort{40'101};
};

void printUsage() {
  std::cerr
      << "Usage:\n"
      << "  syncaudio path/to/file.wav\n"
      << "  syncaudio host path/to/file.wav [--address IPv4] [--port PORT] "
         "[--control-port PORT]\n"
      << "  syncaudio client <host-ip> [--bind IPv4] [--port PORT] "
         "[--control-port PORT]\n";
}

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
    } else {
      throw std::invalid_argument(
          "Unknown, incomplete, or inapplicable option: " +
          std::string(option));
    }
  }
  if (result.audioPort == result.clockSyncPort) {
    throw std::invalid_argument("Audio and control ports must be different");
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

  DesktopNetworkHostConfig config;
  config.destinationAddress = commandLine.destinationAddress;
  config.port = commandLine.audioPort;
  DesktopNetworkHost host(config);
  ClockSyncServer clockServer("0.0.0.0", commandLine.clockSyncPort);

  std::cout << "UDP destination: " << config.destinationAddress << ':'
            << config.port << '\n'
            << "Clock-sync port: " << commandLine.clockSyncPort << '\n'
            << "PCM format:      signed 16-bit little-endian\n"
            << "Playing locally and streaming...\n";

  const auto scheduledStart = PlaybackClock::now() + config.sendAhead;
  player.playAt(scheduledStart);
  const auto playbackStart = player.expectedPlaybackTimestamp(0);
  if (!playbackStart) {
    throw std::runtime_error("Playback timeline did not start");
  }

  const auto &metadata = player.metadata();
  const auto stats =
      host.streamFile(commandLine.argument, *playbackStart, metadata.sampleRate,
                      static_cast<std::uint16_t>(metadata.channels));
  waitForPlayback(player);

  std::cout << "Network stream finished.\n"
            << "Session ID:      " << stats.sessionId << '\n'
            << "Packets sent:    " << stats.packetsSent << '\n'
            << "Frames sent:     " << stats.framesSent << '\n'
            << "Frames/packet:   " << stats.framesPerPacket << '\n'
            << "Packet duration: " << stats.packetDurationMilliseconds
            << " ms\n";
  printPlaybackSummary(player);
}

void runClient(const CommandLine &commandLine) {
  DesktopAudioClientConfig config;
  config.bindAddress = commandLine.bindAddress;
  config.audioPort = commandLine.audioPort;
  config.clockSyncPort = commandLine.clockSyncPort;

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
            << "Final buffer depth:        "
            << stats.finalBufferDepthMilliseconds << " ms\n"
            << "Peak buffer depth:         "
            << stats.peakBufferDepthMilliseconds << " ms\n"
            << "Host-client clock offset:  " << stats.clockOffsetMilliseconds
            << " ms\n"
            << "Clock-sync round-trip:     " << stats.clockRoundTripMilliseconds
            << " ms\n"
            << "Estimated playback delay: "
            << stats.estimatedPlaybackDelayMilliseconds << " ms\n"
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
