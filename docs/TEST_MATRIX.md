# Test matrix

`PASS` below means an automated test completed. No entry in the hardware
section is inferred from a simulator or from host-side loopback traffic.

## Automated

| Scenario | Environment | Result | Evidence |
| --- | --- | --- | --- |
| Windows desktop unit/integration | local Windows Debug | PASS, 14 CTest targets | `ctest -C Debug` |
| Windows desktop unit/integration | local Windows Release | PASS, 14 CTest targets | `ctest -C Release` |
| Packet impairment loss/jitter/delay | seeded in-process simulator | PASS, 100 combinations | `network_impairment_tests` |
| Blackout | 100, 500, 1000, 2000, 5000 ms simulated | PASS | `network_impairment_tests` |
| Duplication/reordering | seeded simulator | PASS | `network_impairment_tests` |
| Drift | ±10, ±50, ±100, ±500 ppm with one outlier | PASS | `reliability_stress_tests` |
| Malformed datagrams | 20,000 random inputs and 52 header mutations | PASS, no crash | `reliability_stress_tests` |
| Route-dependent output latency math | C++ core + Swift simulator | PASS | `output_latency_tests`, iOS unit test |
| Host manual latency smoke | local Windows Debug, 6-second 48 kHz mono WAV, +25 ms | PASS, 600 generated packets and 288,000 frames | CLI loopback run; acoustic timing unmeasured |
| Room model and control protocol | local Windows Debug/Release | PASS | `room_tests`, `control_channel_tests` |
| 1/2/5/10 room clients | local Windows loopback, 8 kHz mono | PASS, 25 received packets per client | `room_transport_tests`; see [rooms](ROOMS.md) |
| Pause, resume, seek, stop | local Windows Debug, 40-second silent WAV | PASS, four scheduled commands and clean stop | CLI host/control run; iPhone acoustic response unmeasured |
| iPhone simulator compile/unit tests | macOS GitHub Actions | PENDING Phase 13 run | Actions workflow |

These tests verify deterministic packet handling and bounds. They do not
measure sound from a physical speaker or iOS recovery after a real outage.

## Reproduce the impairment scenarios

Build Debug, then run the host with a known WAV and options such as:

```powershell
.\build\Debug\syncaudio.exe host .\test.wav --address 192.168.1.255 `
  --impair-loss 5 --impair-delay 50 --impair-jitter 20 `
  --impair-duplicate 1 --impair-reorder 5 --impair-blackout 500 `
  --impair-seed 493
```

All options affect audio datagrams only, after the host generates them. Clock
and TCP control traffic remain untouched. The blackout starts with the first
audio datagram. Release builds reject these options. The host's packet count
reports generated packets, so the simulator test is the source of delivery
counts. For real playback measurements, export the iPhone diagnostics while
the test is active and make a recording that includes both speakers.

## Physical hardware

| Pair | Routes/network | Result | Sync error | Recovery | Notes |
| --- | --- | --- | --- | --- | --- |
| Windows → Windows | built-in / same LAN | NOT TESTED — HARDWARE REQUIRED | unavailable | unavailable | second Windows device required |
| Windows → iPhone | built-in / same Wi-Fi | NOT TESTED — HARDWARE REQUIRED | unavailable | unavailable | user will test at end |
| Windows → iPhone | wired output | NOT TESTED — HARDWARE REQUIRED | unavailable | unavailable | user will test at end |
| Windows → iPhone | Bluetooth output | NOT TESTED — HARDWARE REQUIRED | unavailable | unavailable | user will test at end |
| Windows → Android | any | NOT TESTED — HARDWARE REQUIRED | unavailable | unavailable | Android client not implemented |
| Mac → iPhone | any | NOT TESTED — HARDWARE REQUIRED | unavailable | unavailable | Mac host not validated |
| Mac → Android | any | NOT TESTED — HARDWARE REQUIRED | unavailable | unavailable | neither target validated |

For each available pair, record an exported diagnostics JSON, a shared
transient-sound recording, output routes, Wi-Fi layout, and the time from
forced disconnect to resumed playback. Acoustic sync must be calculated from
the recording, not copied from the app's estimated sync error.
