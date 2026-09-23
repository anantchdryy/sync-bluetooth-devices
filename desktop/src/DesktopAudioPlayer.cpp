#include "DesktopAudioPlayer.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <chrono>

#include <miniaudio.h>

namespace {

std::runtime_error miniaudioError(const char *operation, ma_result result) {
  return std::runtime_error(std::string(operation) + ": " +
                            ma_result_description(result));
}

} // namespace

class DesktopAudioPlayer::Impl {
public:
  ~Impl() {
    stop();
    if (decoderInitialized_) {
      ma_decoder_uninit(&decoder_);
    }
  }

  void load(const std::filesystem::path &path) {
    stop();
    playbackClock_.reset();
    submittedFrames_.store(0, std::memory_order_release);
    stoppedFrame_.store(0, std::memory_order_release);
    stoppedElapsedNanoseconds_.store(0, std::memory_order_release);
    if (decoderInitialized_) {
      ma_decoder_uninit(&decoder_);
      decoderInitialized_ = false;
    }

    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
      throw std::runtime_error("Unable to inspect audio file: " +
                               error.message());
    }
    if (!exists || !std::filesystem::is_regular_file(path, error)) {
      throw std::runtime_error(
          "Audio file does not exist or is not a regular file: " +
          path.string());
    }
    if (error) {
      throw std::runtime_error("Unable to inspect audio file: " +
                               error.message());
    }

    const auto result =
        ma_decoder_init_file(path.string().c_str(), nullptr, &decoder_);
    if (result != MA_SUCCESS) {
      throw miniaudioError("Unable to open audio file", result);
    }
    decoderInitialized_ = true;

    ma_uint64 frameCount = 0;
    const auto lengthResult =
        ma_decoder_get_length_in_pcm_frames(&decoder_, &frameCount);
    if (lengthResult != MA_SUCCESS) {
      ma_decoder_uninit(&decoder_);
      decoderInitialized_ = false;
      throw miniaudioError("Unable to read audio duration", lengthResult);
    }

    metadata_.sampleRate = decoder_.outputSampleRate;
    metadata_.channels = decoder_.outputChannels;
    metadata_.durationSeconds =
        metadata_.sampleRate == 0
            ? 0.0
            : static_cast<double>(frameCount) /
                  static_cast<double>(metadata_.sampleRate);
  }

  void play() { playAt(PlaybackClock::now()); }

  void playAt(PlaybackClock::Timestamp startTime) {
    if (!decoderInitialized_) {
      throw std::logic_error("No audio file has been loaded");
    }

    stop();
    playbackClock_.reset();
    submittedFrames_.store(0, std::memory_order_release);
    stoppedFrame_.store(0, std::memory_order_release);
    stoppedElapsedNanoseconds_.store(0, std::memory_order_release);
    const auto seekResult = ma_decoder_seek_to_pcm_frame(&decoder_, 0);
    if (seekResult != MA_SUCCESS) {
      throw miniaudioError("Unable to rewind audio file", seekResult);
    }

    bytesPerFrame_ = ma_get_bytes_per_frame(decoder_.outputFormat,
                                           decoder_.outputChannels);
    ringCapacityFrames_ = std::max<std::uint64_t>(metadata_.sampleRate, 4096);
    ring_.resize(static_cast<std::size_t>(ringCapacityFrames_) * bytesPerFrame_);
    readFrame_.store(0, std::memory_order_relaxed);
    writtenFrame_.store(0, std::memory_order_relaxed);
    decoderFinished_.store(false, std::memory_order_relaxed);
    prefetchRunning_.store(true, std::memory_order_release);
    prefetchThread_ = std::thread([this] { prefetch(); });
    while (writtenFrame_.load(std::memory_order_acquire) <
               std::min<std::uint64_t>(metadata_.sampleRate / 4, ringCapacityFrames_) &&
           !decoderFinished_.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = decoder_.outputFormat;
    config.playback.channels = decoder_.outputChannels;
    config.sampleRate = decoder_.outputSampleRate;
    config.periodSizeInMilliseconds = 10;
    config.dataCallback = &Impl::dataCallback;
    config.pUserData = this;

    auto result = ma_device_init(nullptr, &config, &device_);
    if (result != MA_SUCCESS) {
      prefetchRunning_.store(false, std::memory_order_release);
      prefetchThread_.join();
      throw miniaudioError("Unable to initialize the audio device", result);
    }
    deviceInitialized_ = true;
    endReached_.store(false, std::memory_order_release);
    submittedFrames_.store(0, std::memory_order_release);
    stoppedFrame_.store(0, std::memory_order_release);
    stoppedElapsedNanoseconds_.store(0, std::memory_order_release);
    playbackClock_.emplace(metadata_.sampleRate, startTime);
    timelineRunning_.store(true, std::memory_order_release);
    playing_.store(true, std::memory_order_release);

    result = ma_device_start(&device_);
    if (result != MA_SUCCESS) {
      playing_.store(false, std::memory_order_release);
      timelineRunning_.store(false, std::memory_order_release);
      playbackClock_.reset();
      ma_device_uninit(&device_);
      deviceInitialized_ = false;
      prefetchRunning_.store(false, std::memory_order_release);
      prefetchThread_.join();
      throw miniaudioError("Unable to start the audio device", result);
    }
  }

  void stop() noexcept {
    freezeTimeline();
    playing_.store(false, std::memory_order_release);
    if (deviceInitialized_) {
      ma_device_uninit(&device_);
      deviceInitialized_ = false;
    }
    prefetchRunning_.store(false, std::memory_order_release);
    if (prefetchThread_.joinable()) prefetchThread_.join();
  }

  [[nodiscard]] bool isPlaying() const noexcept {
    return playing_.load(std::memory_order_acquire);
  }

  [[nodiscard]] bool isLoaded() const noexcept { return decoderInitialized_; }

  [[nodiscard]] const AudioMetadata &metadata() const noexcept {
    return metadata_;
  }

  [[nodiscard]] PlaybackClock::Frame currentPlaybackFrame() const noexcept {
    if (!playbackClock_) {
      return 0;
    }
    if (!timelineRunning_.load(std::memory_order_acquire)) {
      return stoppedFrame_.load(std::memory_order_acquire);
    }

    try {
      const auto timelineFrame =
          playbackClock_->timestampToFrame(PlaybackClock::now());
      const auto submitted = submittedFrames_.load(std::memory_order_acquire);
      return timelineFrame < submitted ? timelineFrame : submitted;
    } catch (const std::overflow_error &) {
      return submittedFrames_.load(std::memory_order_acquire);
    }
  }

  [[nodiscard]] std::optional<PlaybackClock::Timestamp>
  expectedPlaybackTimestamp() const {
    return expectedPlaybackTimestamp(currentPlaybackFrame());
  }

  [[nodiscard]] std::optional<PlaybackClock::Timestamp>
  expectedPlaybackTimestamp(PlaybackClock::Frame frame) const {
    if (!playbackClock_) {
      return std::nullopt;
    }
    return playbackClock_->frameToTimestamp(frame);
  }

  [[nodiscard]] PlaybackClock::Duration elapsedPlaybackTime() const noexcept {
    if (!playbackClock_) {
      return PlaybackClock::Duration::zero();
    }
    if (timelineRunning_.load(std::memory_order_acquire)) {
      return playbackClock_->elapsed();
    }
    return PlaybackClock::Duration{
        stoppedElapsedNanoseconds_.load(std::memory_order_acquire)};
  }

private:
  void prefetch() noexcept {
    while (prefetchRunning_.load(std::memory_order_acquire)) {
      const auto written = writtenFrame_.load(std::memory_order_relaxed);
      const auto read = readFrame_.load(std::memory_order_acquire);
      const auto available = ringCapacityFrames_ - (written - read);
      if (available == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      const auto contiguous = ringCapacityFrames_ - written % ringCapacityFrames_;
      const auto request = std::min<std::uint64_t>({available, contiguous, 4096});
      ma_uint64 framesRead = 0;
      const auto result = ma_decoder_read_pcm_frames(
          &decoder_, ring_.data() +
                         static_cast<std::size_t>(written % ringCapacityFrames_) * bytesPerFrame_,
          request, &framesRead);
      writtenFrame_.store(written + framesRead, std::memory_order_release);
      if (result != MA_SUCCESS || framesRead < request) {
        decoderFinished_.store(true, std::memory_order_release);
        return;
      }
    }
  }

  void freezeTimeline(bool streamCompleted = false) noexcept {
    if (!timelineRunning_.load(std::memory_order_acquire) || !playbackClock_) {
      return;
    }

    const auto stoppedFrame =
        streamCompleted ? submittedFrames_.load(std::memory_order_acquire)
                        : currentTimelineFrame();
    auto stoppedElapsed = playbackClock_->elapsed();
    if (streamCompleted) {
      try {
        const auto scheduledElapsed =
            playbackClock_->frameToTimestamp(stoppedFrame) -
            playbackClock_->startTime();
        if (scheduledElapsed > stoppedElapsed) {
          stoppedElapsed = scheduledElapsed;
        }
      } catch (const std::overflow_error &) {
        // A real decoded stream cannot approach the clock's numeric limit.
      }
    }

    stoppedFrame_.store(stoppedFrame, std::memory_order_release);
    stoppedElapsedNanoseconds_.store(stoppedElapsed.count(),
                                     std::memory_order_release);
    timelineRunning_.store(false, std::memory_order_release);
  }

  [[nodiscard]] PlaybackClock::Frame currentTimelineFrame() const noexcept {
    try {
      const auto timelineFrame =
          playbackClock_->timestampToFrame(PlaybackClock::now());
      const auto submitted = submittedFrames_.load(std::memory_order_acquire);
      return timelineFrame < submitted ? timelineFrame : submitted;
    } catch (const std::overflow_error &) {
      return submittedFrames_.load(std::memory_order_acquire);
    }
  }

  static void dataCallback(ma_device *device, void *output, const void *,
                           ma_uint32 frameCount) {
    auto *self = static_cast<Impl *>(device->pUserData);
    const auto bytesPerFrame = self->bytesPerFrame_;
    std::memset(output, 0,
                static_cast<std::size_t>(frameCount) * bytesPerFrame);

    if (self->playbackClock_ &&
        PlaybackClock::now() < self->playbackClock_->startTime()) {
      return;
    }

    if (self->endReached_.load(std::memory_order_acquire)) {
      // One silent callback allows the final decoded buffer to drain to the
      // device.
      self->freezeTimeline(true);
      self->playing_.store(false, std::memory_order_release);
      return;
    }

    const auto read = self->readFrame_.load(std::memory_order_relaxed);
    const auto written = self->writtenFrame_.load(std::memory_order_acquire);
    const auto framesRead = std::min<std::uint64_t>(frameCount, written - read);
    const auto first = std::min<std::uint64_t>(
        framesRead, self->ringCapacityFrames_ - read % self->ringCapacityFrames_);
    std::memcpy(output,
                self->ring_.data() + static_cast<std::size_t>(read % self->ringCapacityFrames_) * bytesPerFrame,
                static_cast<std::size_t>(first) * bytesPerFrame);
    if (framesRead > first) {
      std::memcpy(static_cast<std::byte *>(output) + first * bytesPerFrame,
                  self->ring_.data(),
                  static_cast<std::size_t>(framesRead - first) * bytesPerFrame);
    }
    self->readFrame_.store(read + framesRead, std::memory_order_release);
    self->submittedFrames_.fetch_add(framesRead, std::memory_order_release);
    if (self->decoderFinished_.load(std::memory_order_acquire) &&
        read + framesRead == written) {
      self->endReached_.store(true, std::memory_order_release);
    }
  }

  ma_decoder decoder_{};
  ma_device device_{};
  std::vector<std::byte> ring_;
  std::uint64_t ringCapacityFrames_{0};
  ma_uint32 bytesPerFrame_{0};
  std::atomic<std::uint64_t> readFrame_{0};
  std::atomic<std::uint64_t> writtenFrame_{0};
  std::atomic_bool prefetchRunning_{false};
  std::atomic_bool decoderFinished_{false};
  std::thread prefetchThread_;
  bool decoderInitialized_{false};
  bool deviceInitialized_{false};
  std::atomic_bool playing_{false};
  std::atomic_bool endReached_{false};
  std::atomic_bool timelineRunning_{false};
  std::atomic<PlaybackClock::Frame> submittedFrames_{0};
  std::atomic<PlaybackClock::Frame> stoppedFrame_{0};
  std::atomic<PlaybackClock::Duration::rep> stoppedElapsedNanoseconds_{0};
  std::optional<PlaybackClock> playbackClock_;
  AudioMetadata metadata_{};
};

DesktopAudioPlayer::DesktopAudioPlayer() : impl_(std::make_unique<Impl>()) {}
DesktopAudioPlayer::~DesktopAudioPlayer() = default;

void DesktopAudioPlayer::load(const std::filesystem::path &path) {
  impl_->load(path);
}

void DesktopAudioPlayer::play() { impl_->play(); }

void DesktopAudioPlayer::playAt(PlaybackClock::Timestamp startTime) {
  impl_->playAt(startTime);
}

void DesktopAudioPlayer::stop() noexcept { impl_->stop(); }

bool DesktopAudioPlayer::isPlaying() const noexcept {
  return impl_->isPlaying();
}

bool DesktopAudioPlayer::isLoaded() const noexcept { return impl_->isLoaded(); }

const AudioMetadata &DesktopAudioPlayer::metadata() const noexcept {
  return impl_->metadata();
}

PlaybackClock::Frame DesktopAudioPlayer::currentPlaybackFrame() const noexcept {
  return impl_->currentPlaybackFrame();
}

std::optional<PlaybackClock::Timestamp>
DesktopAudioPlayer::expectedPlaybackTimestamp() const {
  return impl_->expectedPlaybackTimestamp();
}

std::optional<PlaybackClock::Timestamp>
DesktopAudioPlayer::expectedPlaybackTimestamp(
    PlaybackClock::Frame frame) const {
  return impl_->expectedPlaybackTimestamp(frame);
}

PlaybackClock::Duration
DesktopAudioPlayer::elapsedPlaybackTime() const noexcept {
  return impl_->elapsedPlaybackTime();
}
