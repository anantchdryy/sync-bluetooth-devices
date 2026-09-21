# SyncAudio

The project currently contains the first two desktop phases: local WAV playback
and a reusable monotonic playback timeline. Networking, multi-device
synchronization, Bluetooth management, compression, GUI, and iPhone support are
intentionally not implemented yet.

## Requirements

- CMake 3.20 or newer
- A C++20 compiler (Visual Studio 2022, recent Clang, or recent GCC)
- An audio output device
- Internet access during the first CMake configure; CMake downloads the pinned
  miniaudio 0.11.25 source archive

miniaudio has no additional runtime dependency on Windows or macOS. Linux needs
the usual development packages for its audio backends (for example ALSA and/or
PulseAudio) when they are not already installed.

## Build

### Windows (Visual Studio)

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### macOS or Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

On Windows with a multi-configuration generator:

```powershell
.\build\Release\syncaudio.exe C:\path\to\file.wav
```

On macOS/Linux, or with a single-configuration generator:

```sh
./build/syncaudio /path/to/file.wav
```

The program prints the decoded sample rate, channel count, and duration, plays
the file through the default audio output device, and exits when playback ends.
It then reports the final playback frame, elapsed monotonic time, and the
frame's expected steady-clock timestamp.

## Playback timeline

`PlaybackClock` maps PCM frame numbers to `std::chrono::steady_clock`
timestamps using the file's sample rate. It uses nanosecond durations and
integer conversion arithmetic, and never depends on the system/wall clock.

`DesktopAudioPlayer` exposes the current timeline frame, that frame's expected
timestamp, and elapsed playback time. The current estimate is capped at audio
frames already submitted to miniaudio. This phase does not yet compensate for
audio-driver, device, wired, or Bluetooth output latency.

## Project layout

- `core/include` and `core/src` — portable playback clock
- `desktop/include` — desktop audio player public interface
- `desktop/src` — miniaudio-backed implementation and CLI
- `tests` — clock conversion/edge-case tests plus file handling and WAV
  metadata tests that do not require an audio device
- `core` and `ios` are deferred until a phase requires them
