#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

struct AudioMetadata {
  std::uint32_t sampleRate{};
  std::uint32_t channels{};
  double durationSeconds{};
};

class DesktopAudioPlayer {
public:
  DesktopAudioPlayer();
  ~DesktopAudioPlayer();

  DesktopAudioPlayer(const DesktopAudioPlayer &) = delete;
  DesktopAudioPlayer &operator=(const DesktopAudioPlayer &) = delete;
  DesktopAudioPlayer(DesktopAudioPlayer &&) = delete;
  DesktopAudioPlayer &operator=(DesktopAudioPlayer &&) = delete;

  // Opens and inspects an audio file. Calling load() stops any current
  // playback.
  void load(const std::filesystem::path &path);

  // Starts playback from the beginning of the loaded file.
  void play();
  void stop() noexcept;

  [[nodiscard]] bool isPlaying() const noexcept;
  [[nodiscard]] bool isLoaded() const noexcept;
  [[nodiscard]] const AudioMetadata &metadata() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
