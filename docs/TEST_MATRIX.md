# Test matrix

`PASS` below means an automated test completed. No entry in the hardware
section is inferred from a simulator or from host-side loopback traffic.

## Automated

| Scenario | Environment | Result | Evidence |
| --- | --- | --- | --- |
| Windows desktop unit/integration | local Windows Debug | PASS, 11 CTest targets | `ctest -C Debug` |
| Packet impairment loss/jitter/delay | seeded in-process simulator | PASS, 100 combinations | `network_impairment_tests` |
| Blackout | 100, 500, 1000, 2000, 5000 ms simulated | PASS | `network_impairment_tests` |
| Duplication/reordering | seeded simulator | PASS | `network_impairment_tests` |
| Drift | ±10, ±50, ±100, ±500 ppm with one outlier | PASS | `reliability_stress_tests` |
| Malformed datagrams | 20,000 random inputs and 52 header mutations | PASS, no crash | `reliability_stress_tests` |
| iPhone simulator compile/unit tests | macOS GitHub Actions | PENDING latest Phase 11 run | Actions workflow |

These tests verify deterministic packet handling and bounds. They do not
measure sound from a physical speaker or iOS recovery after a real outage.

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
