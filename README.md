# SyncAudio

The project currently contains local WAV playback, a reusable monotonic
playback timeline, and a UDP desktop host that sends raw PCM while continuing
local playback. Client playback, clock synchronization, Bluetooth management,
compression, GUI, and iPhone support are intentionally not implemented yet.

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

## UDP host

Start local playback and broadcast PCM packets on the LAN using the default UDP
port `40100`:

```powershell
.\build\Release\syncaudio.exe host "C:\path\to\file.wav"
```

Use a specific IPv4 destination and port with:

```powershell
.\build\Release\syncaudio.exe host "C:\path\to\file.wav" --address 192.168.1.50 --port 40100
```

For a local packet capture, send to loopback:

```powershell
.\build\Release\syncaudio.exe host "C:\path\to\file.wav" --address 127.0.0.1 --port 40100
```

Capture with Wireshark using `udp.port == 40100`. On Windows, capturing loopback
traffic requires the Npcap loopback adapter. Each datagram begins with ASCII
`SAUD` (`53 41 55 44` in hex) and is at most 1,200 bytes.

Windows Firewall may prompt for network access when broadcast mode is first
used. Only private-network access is needed for LAN testing.

### Audio packet wire format (version 1)

The header is exactly 48 bytes. Multi-byte header integers use network byte
order (big-endian); PCM samples are interleaved signed 16-bit little-endian.
No C/C++ struct is copied directly to the wire, so compiler padding cannot
change the format.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic: ASCII `SAUD` |
| 4 | 1 | Protocol version (`1`) |
| 5 | 1 | Header size (`48`) |
| 6 | 2 | Flags (currently `0`) |
| 8 | 8 | Stream/session ID |
| 16 | 4 | Sequence number |
| 20 | 4 | Sample rate |
| 24 | 2 | Channel count |
| 26 | 1 | Sample format (`1` = signed 16-bit little-endian PCM) |
| 27 | 1 | Reserved (`0`) |
| 28 | 8 | Starting audio-frame index |
| 36 | 8 | Host steady-clock presentation timestamp, nanoseconds |
| 44 | 2 | PCM payload size in bytes |
| 46 | 2 | PCM frame count |
| 48 | variable | Interleaved PCM payload, at most 1,152 bytes |

Packets are sent approximately 100 ms ahead of their presentation timestamp.
The 1,200-byte datagram ceiling avoids typical IP fragmentation. At 44.1 kHz
stereo this limit produces 288-frame packets (about 6.53 ms); the shorter than
preferred duration is necessary while sending uncompressed PCM. The monotonic
timestamp is local to the host and will become meaningful to clients after a
future clock-synchronization phase.

## Project layout

- `core/include` and `core/src` — portable playback clock
- `network/include` and `network/src` — packet format, serialization, UDP sender
- `desktop/include` — desktop audio player public interface
- `desktop/src` — miniaudio-backed implementation and CLI
- `tests` — packet serialization, clock conversion/edge-case, file handling,
  and WAV metadata tests that do not require an audio device
- `ios` is deferred until the iOS phase
