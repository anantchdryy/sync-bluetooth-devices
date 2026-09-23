# Phase 14 release report

## Architecture and feature status

Tandem Audio comprises a Windows WPF desktop app and C++ CLI/engine, a SwiftUI iPhone app, and a platform-neutral C++ core. The desktop app creates a named room around a PCM WAV file or discovers and joins a nearby room. It shows playback controls, room members, connection quality, route label, settings, and developer logs. The iPhone has onboarding, nearby-room discovery, room, device, and settings screens; diagnostics are behind Developer Mode. A self-contained per-user NSIS Windows installer contains the WPF app, native engine, metadata, and icon. iPhone app icon and Xcode signing/archive configuration are present; a signed iPhone archive requires the user's Apple team on a Mac.

Wi-Fi carries discovery (`_tandemaudio._tcp` DNS-SD), bounded TCP room control, UDP PCM audio unicast per member, and `SCLK` clock probes. Bluetooth is an optional OS-managed speaker route, never the synchronization transport. Audio packets contain monotonic host presentation timestamps and frame numbers. Clients estimate host offset with repeated low-RTT four-timestamp probes, retain a bounded jitter buffer, and adjust drift gradually rather than dropping large chunks. Output scheduling uses platform latency estimates and optional route-specific manual compensation. The host schedules play/pause/seek/stop ahead for the room. There is no acoustic microphone calibration or claim of acoustic alignment yet.

See [architecture](ARCHITECTURE.md), [Wi-Fi transport](WIFI_ARCHITECTURE.md), [protocol](PROTOCOL.md), [rooms](ROOMS.md), and [output latency](OUTPUT_LATENCY.md).

## Tested on real hardware

The development Windows PC built Debug and Release C++ targets, passed all 14 CTest targets in each configuration, built the Release WPF app, and produced the installer. A local Windows loopback run joined a room and received 750 packets with 0% estimated loss; it did not involve a second speaker or real Wi-Fi path. A silent installer run placed the two executables and license in a temporary directory; the engine launched and the silent uninstaller exited with code 0 and removed the directory. This is local installation smoke testing, not a clean independent PC test.

No physical iPhone playback or acoustic synchronization measurement has been performed. No iPhone speaker, wired accessory, Bluetooth accessory, or second Windows machine has been tested.

## Tested through simulation and CI

The local Release suite passed 14/14 CTests, including packet parsing, clock synchronization, drift correction, jitter buffering, output latency math, network impairment, reliability stress, room state/control, and 1/2/5/10-client loopback delivery. The seeded impairment test covers 100 loss/jitter/delay combinations, blackouts up to 5 seconds, duplication, and reordering. The stress test covers clock rates from -500 to +500 ppm and 20,000 malformed datagrams. At 8 kHz mono, the 10-client short loopback run delivered 250 datagrams / 53,000 payload bytes and reported 0.241 CPU seconds and 7.09 MiB process working set on the development PC; this is a short local microbenchmark, not a Wi-Fi performance result. Details are in [test matrix](TEST_MATRIX.md) and [rooms](ROOMS.md).

GitHub Windows and macOS/iPhone simulator workflows passed on the Phase 14 UI commit `9696473`. The icon/asset changes need a fresh CI run after this commit. Simulator compile/tests do not exercise physical speaker timing, LAN discovery across devices, or background audio on a real iPhone.

## Not yet tested

| Required check | Status |
| --- | --- |
| Automatic discovery between Windows and physical iPhone | Pending user device test |
| iPhone room join and audible playback | Pending user device test |
| Acoustic synchronization accuracy and approximately 20 ms goal | **Unavailable; not measured** |
| Pause/resume/seek across physical outputs | Pending user device test |
| Wi-Fi disconnect/reconnect duration | **Unavailable; not measured** |
| iPhone built-in, wired, Bluetooth route changes | Pending user device test |
| Background/screen-lock/interruptions on physical iPhone | Pending user device test |
| CPU, memory, network bandwidth in sustained physical room | **Unavailable; not measured** |
| Clean install on independent Windows computer | Pending environment access |
| Signed iPhone development/archive artifact | Pending Mac/Xcode/team access |

The code uses uncompressed PCM. For 48 kHz stereo signed 16-bit audio, raw payload is 192,000 bytes/s per client before packet and network overhead; this is arithmetic, **not measured bandwidth**. The actual 8 kHz mono loopback microbenchmark above must not be extrapolated as a sustained multi-device result.

## Audit and limits

The Release build and tests pass, the GUI has zero .NET build warnings, and the Windows installer install/uninstall smoke passed. Protocol parsing checks sizes and identity, queues are bounded, Release rejects impairment flags, local playback commands are loopback-only, and the installer avoids broad firewall changes. The current room cap is 32 control clients, which is a safety bound rather than a supported synchronized-hardware count. There is no encryption/authentication on the LAN protocol. Windows GUI connection-quality labels are coarse estimates. No separate sanitizer or static-analysis run, independent clean-machine install, or prolonged hardware race/route audit has been completed. See [known limitations](KNOWN_LIMITATIONS.md).

## Next priorities

1. Install the iPhone app using a borrowed/hosted Mac with Xcode, then run the [physical test plan](TESTING.md).
2. Measure acoustic click separation on built-in, wired, and Bluetooth routes; use the results to tune route compensation.
3. Run prolonged multi-device Wi-Fi, disconnect/reconnect, screen-lock, and background tests; capture diagnostics and resource use.
4. Only after those results, decide whether automatic acoustic calibration and protocol security are needed for a broader release.

No App Store, Microsoft Store, or public release upload is part of this phase. The Windows installer is a local artifact; source commits are pushed to GitHub as previously requested.
