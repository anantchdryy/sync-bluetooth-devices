#include "DesktopAudioPlayer.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

template <typename T> void writeLittleEndian(std::ofstream &stream, T value) {
  for (std::size_t byte = 0; byte < sizeof(T); ++byte) {
    stream.put(static_cast<char>((value >> (byte * 8U)) & 0xFFU));
  }
}

void writeTestWav(const std::filesystem::path &path) {
  constexpr std::uint32_t sampleRate = 8'000;
  constexpr std::uint16_t channels = 1;
  constexpr std::uint16_t bitsPerSample = 16;
  constexpr std::uint32_t sampleCount = 2'000;
  constexpr std::uint32_t dataSize = sampleCount * sizeof(std::int16_t);

  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Could not create test WAV file");
  }

  output.write("RIFF", 4);
  writeLittleEndian(output, 36U + dataSize);
  output.write("WAVEfmt ", 8);
  writeLittleEndian(output, 16U);
  writeLittleEndian(output, static_cast<std::uint16_t>(1));
  writeLittleEndian(output, channels);
  writeLittleEndian(output, sampleRate);
  writeLittleEndian(output, sampleRate * channels * bitsPerSample / 8U);
  writeLittleEndian(output,
                    static_cast<std::uint16_t>(channels * bitsPerSample / 8U));
  writeLittleEndian(output, bitsPerSample);
  output.write("data", 4);
  writeLittleEndian(output, dataSize);

  const std::array<char, dataSize> silence{};
  output.write(silence.data(), static_cast<std::streamsize>(silence.size()));
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

} // namespace

int main() {
  const auto wavPath = std::filesystem::temp_directory_path() /
                       "syncaudio_desktop_audio_player_test.wav";

  try {
    {
      DesktopAudioPlayer player;
      require(!player.isLoaded(), "A new player must not be loaded");

      bool missingFileRejected = false;
      try {
        player.load(wavPath.string() + ".missing");
      } catch (const std::runtime_error &) {
        missingFileRejected = true;
      }
      require(missingFileRejected, "A missing file must be rejected");

      writeTestWav(wavPath);
      player.load(wavPath);
      require(player.isLoaded(), "The generated WAV file should load");

      const auto &metadata = player.metadata();
      require(metadata.sampleRate == 8'000, "Unexpected sample rate");
      require(metadata.channels == 1, "Unexpected channel count");
      require(std::abs(metadata.durationSeconds - 0.25) < 0.0001,
              "Unexpected duration");
    }

    std::filesystem::remove(wavPath);
    std::cout << "DesktopAudioPlayer tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::filesystem::remove(wavPath);
    std::cerr << "Test failure: " << error.what() << '\n';
    return 1;
  }
}
