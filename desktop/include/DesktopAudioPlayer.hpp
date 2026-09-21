#pragma once

#include "PlaybackClock.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

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
  [[nodiscard]] PlaybackClock::Frame currentPlaybackFrame() const noexcept;
  [[nodiscard]] std::optional<PlaybackClock::Timestamp>
  expectedPlaybackTimestamp() const;
  [[nodiscard]] std::optional<PlaybackClock::Timestamp>
  expectedPlaybackTimestamp(PlaybackClock::Frame frame) const;
  [[nodiscard]] PlaybackClock::Duration elapsedPlaybackTime() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
