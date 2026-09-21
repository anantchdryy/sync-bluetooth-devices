# SyncAudio

Phase 1 is a desktop command-line application that loads and plays a local WAV
file. Networking, synchronization, Bluetooth management, compression, GUI, and
iPhone support are intentionally out of scope for this phase.

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

## Project layout

- `desktop/include` — desktop audio player public interface
- `desktop/src` — miniaudio-backed implementation and CLI
- `tests` — tests that validate file handling and WAV metadata without opening
  an audio device
- `core` and `ios` are deferred until a phase requires them
