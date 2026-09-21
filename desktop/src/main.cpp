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

struct CommandLine {
  bool hostMode{false};
  std::string audioPath;
  std::string destinationAddress{"255.255.255.255"};
  std::uint16_t port{40'100};
};

void printUsage() {
  std::cerr
      << "Usage:\n"
      << "  syncaudio path/to/file.wav\n"
      << "  syncaudio host path/to/file.wav [--address IPv4] [--port PORT]\n";
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
    return CommandLine{false, argv[1]};
  }
  if (argc < 3 || std::string_view(argv[1]) != "host") {
    throw std::invalid_argument("Invalid command line");
  }

  CommandLine result;
  result.hostMode = true;
  result.audioPath = argv[2];
  for (int index = 3; index < argc; ++index) {
    const std::string_view option = argv[index];
    if (option == "--address" && index + 1 < argc) {
      result.destinationAddress = argv[++index];
    } else if (option == "--port" && index + 1 < argc) {
      result.port = parsePort(argv[++index]);
    } else {
      throw std::invalid_argument("Unknown or incomplete option: " +
                                  std::string(option));
    }
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
  player.load(commandLine.audioPath);
  printMetadata(player.metadata());
  std::cout << "Playing...\n";
  player.play();
  waitForPlayback(player);
  printPlaybackSummary(player);
}

void runHost(const CommandLine &commandLine) {
  DesktopAudioPlayer player;
  player.load(commandLine.audioPath);
  printMetadata(player.metadata());

  DesktopNetworkHostConfig config;
  config.destinationAddress = commandLine.destinationAddress;
  config.port = commandLine.port;
  DesktopNetworkHost host(config);

  std::cout << "UDP destination: " << config.destinationAddress << ':'
            << config.port << '\n'
            << "PCM format:      signed 16-bit little-endian\n"
            << "Playing locally and streaming...\n";

  player.play();
  const auto playbackStart = player.expectedPlaybackTimestamp(0);
  if (!playbackStart) {
    throw std::runtime_error("Playback timeline did not start");
  }

  const auto &metadata = player.metadata();
  const auto stats = host.streamFile(
      commandLine.audioPath, *playbackStart, metadata.sampleRate,
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

} // namespace

int main(int argc, char *argv[]) {
  try {
    const auto commandLine = parseCommandLine(argc, argv);
    if (commandLine.hostMode) {
      runHost(commandLine);
    } else {
      runLocal(commandLine);
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
