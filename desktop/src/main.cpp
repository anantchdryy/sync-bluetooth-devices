#include "DesktopAudioPlayer.hpp"

#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <thread>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: syncaudio path/to/file.wav\n";
    return 2;
  }

  try {
    DesktopAudioPlayer player;
    player.load(argv[1]);

    const auto &metadata = player.metadata();
    std::cout << "Sample rate: " << metadata.sampleRate << " Hz\n"
              << "Channels:    " << metadata.channels << '\n'
              << "Duration:    " << std::fixed << std::setprecision(2)
              << metadata.durationSeconds << " seconds\n"
              << "Playing...\n";

    player.play();
    while (player.isPlaying()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    player.stop();
    std::cout << "Playback finished.\n";
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
