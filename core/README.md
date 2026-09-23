# Portable audio core

`syncaudio_core` is a C++20 static library. It builds independently of the
desktop app and miniaudio:

```sh
cmake -S core -B build-core
cmake --build build-core --config Release
```

All public headers are in `core/include`. The library uses only the C++ standard
library and contains no operating-system sockets, audio device APIs, Swift,
Objective-C, AVFoundation, or GUI code.

The planned iOS client can reuse:

| Header | Portable responsibility |
| --- | --- |
| `AudioPacket.hpp`, `PacketSerializer.hpp` | Decode and validate the desktop host's UDP audio packets |
| `ClockSync.hpp` | Encode clock-sync messages and calculate offset/round-trip time from four timestamps |
| `JitterBuffer.hpp` | Reorder packets and read PCM frames, filling gaps with silence |
| `DriftCorrection.hpp` | Estimate clock-rate differences and calculate subtle playback corrections |
| `PlaybackClock.hpp` | Map audio frames to monotonic timestamps |

The iOS app will provide its own Network.framework transport and AVFoundation
playback. Swift can call the C++ library through a small platform bridge or
Swift's C++ interoperability, depending on the Xcode project setup. The
desktop equivalents live in `desktop/` and link to this same core library.
