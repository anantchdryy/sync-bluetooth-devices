# Tandem Audio

Tandem Audio streams PCM audio from a Windows desktop to another desktop or an
iPhone over a local network. The desktop host plays a WAV file locally while
sending timestamped UDP packets. Clients use clock probes, a jitter buffer,
and scheduled playback to align with the host. The iPhone app shows packet,
playback, and synchronization diagnostics. Bluetooth management and compressed
audio are not implemented. Physical iPhone playback and acoustic alignment
still require device testing; simulator tests alone cannot verify them.

For iPhone setup and measurement guidance, see [ios/README.md](ios/README.md).

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

Packets are sent approximately 500 ms ahead of their presentation timestamp.
The 1,200-byte datagram ceiling avoids typical IP fragmentation. At 44.1 kHz
stereo this limit produces 288-frame packets (about 6.53 ms); the shorter than
preferred duration is necessary while sending uncompressed PCM. The monotonic
timestamp is local to the host and is translated to the client's steady clock
using the clock-offset measurement below.

## Desktop client prototype

The client listens for audio on UDP port `40100` and uses UDP port `40101` for
clock-sync requests. Start the client before the host so it can receive the
beginning of the stream.

To test both processes on one Windows computer, open two PowerShell terminals.
In terminal 1:

```powershell
.\build\Release\syncaudio.exe client 127.0.0.1
```

Then, in terminal 2:

```powershell
.\build\Release\syncaudio.exe host "C:\path\to\file.wav" --address 127.0.0.1
```

For two computers on the same LAN, run this on the client computer, replacing
the address with the host computer's LAN IPv4 address:

```powershell
.\build\Release\syncaudio.exe client 192.168.1.10
```

Then run the host using either the default LAN broadcast destination or the
client computer's LAN IPv4 address:

```powershell
.\build\Release\syncaudio.exe host "C:\path\to\file.wav" --address 192.168.1.20
```

Both machines must permit private-network UDP traffic on ports `40100` and
`40101`. Override them consistently on both commands with `--port` and
`--control-port`. The client bind address can be changed from `0.0.0.0` with
`--bind`.

The host schedules local playback 500 ms into the future, giving a waiting
client time to initialize its audio device. The client selects a future packet,
buffers instead of playing on arrival, and schedules that packet against the
translated host timestamp plus a 20 ms safety delay. Missing packet ranges are
rendered as silence. Final statistics include received, estimated lost,
out-of-order, buffer depth, clock offset, clock round-trip, scheduling delay,
and underrun frames.

### Clock-offset measurement

Clock synchronization uses small `SCLK` UDP messages on the control port. For
each sample:

- the client records `t1` and sends a request;
- the host records receive time `t2` and response-send time `t3`;
- the client records response time `t4`.

The client calculates:

```text
round trip = (t4 - t1) - (t3 - t2)
host - client offset = ((t2 - t1) + (t3 - t4)) / 2
```

Eight samples are requested and the lowest-round-trip sample is used. The client
repeats this measurement every five seconds. `DriftEstimator` fits recent offset
samples to estimate the host clock's rate relative to the client clock in parts
per million. `GradualDriftCorrector` combines that estimate with the current
playback phase error and limits the requested source-frame consumption rate to
within 500 ppm of normal. The client uses linear interpolation between PCM
frames for these small rate changes; it does not discard large audio chunks.
Final client statistics include clock offset, estimated drift, buffer/phase
error, and the correction ratio. Reported playback delay is scheduler timing
and does not include unknown speaker, Bluetooth, or audio-driver output latency.

## Project layout

- `core` — standalone C++20 library with packet serialization, clock sync
  protocol/math, jitter buffer, drift correction, and playback timeline;
  see [iOS reuse notes](core/README.md)
- `desktop/include` and `desktop/src` — desktop UDP socket transport,
  miniaudio-backed playback, scheduling, host/client, and CLI
- `tests` — packet serialization, jitter buffer, clock sync, clock conversion,
  file handling, and WAV metadata tests
- `ios` — Phase 7 SwiftUI iPhone packet receiver; see [iPhone setup](ios/README.md)
