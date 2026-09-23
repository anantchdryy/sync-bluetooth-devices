# Output latency and calibration

Network clock offset, packet transport time, jitter buffer depth, and output
latency have separate values. For a desired physical sound time `T`, the
submission target is `T - estimatedOutputLatency`. A manual adjustment from
−1000 to +1000 ms is added to the system estimate before scheduling. Positive
adjustment submits earlier; negative adjustment submits later.

## Current implementations

- The iPhone reads `AVAudioSession.outputLatency` and `ioBufferDuration` after
  activating playback. It labels that sum a **system estimate**, not a measured
  acoustic delay. `AVAudioSession.currentRoute.outputs.first` supplies the
  output port UID and type. Manual adjustments are stored by UID in local
  `UserDefaults`. A route-change notification clears queued playback, reads
  the new route and its own adjustment, then re-buffers live packets. The app
  never carries the previous route's adjustment to the new route.
- The Windows host CLI accepts `--output-latency-ms N`. It shifts SAUD
  presentation timestamps by `N` without changing the local playback callback.
  `N=0` is the default and has **unknown acoustic accuracy**. The desktop
  client accepts the same option and subtracts it when scheduling its audio
  submission. It reports the miniaudio output name, but does not persist a
  calibration because the name alone is not a reliable hardware identity.
  If miniaudio reports a reroute, the desktop stream stops with a calibration
  error so it cannot silently reuse the old adjustment. The user must restart
  it with a calibration for the new route. Some desktop backends do not report
  reroutes, so this safeguard is incomplete until device testing.
- Bluetooth remains an OS-selected output route. There is no Bluetooth
  device-to-device link, pairing code, or hardcoded Bluetooth delay.

The iPhone's estimated sync error compares engine timing against the network
clock. It cannot prove the two speakers emitted sound together. Headphones,
AirPods, Bluetooth speakers, USB DACs, and OS processing can add delay beyond
the system estimate. A route-dependent adjustment must be checked with a
recording of both speakers.

## Manual procedure

1. Play a WAV containing spaced sharp clicks on the Windows host.
2. Record both outputs with one microphone. Keep the phone and desktop at
   nearly equal distance from the microphone.
3. Compare click onset positions in the recording. A 48 kHz recording has
   48 samples per millisecond. Record the sign and magnitude of the difference.
4. On the iPhone, drag **Manual output calibration** and release to re-buffer
   at the new value. The value is saved for the current output port UID.
5. Repeat at least five clicks and report median error and spread. Change the
   route and verify the adjustment does not carry over.

The host CLI adjustment should be calibrated first if its own speaker path is
known. Avoid tuning both sides at once, since the relative difference can hide
two wrong absolute values.

## Automatic acoustic calibration investigation

A microphone-based estimate requires a known click or chirp with an assigned
host timestamp, microphone capture time, the microphone's own input latency,
and repeated onset detection. Cross-correlation could estimate an arrival
time, while median filtering could reject single reflections. A room echo,
other nearby speakers, operating-system echo processing, and uncertain input
latency can make that estimate biased. An iPhone microphone also changes the
audio session and may route playback differently. An automated value should
be applied only after multiple real-device trials show repeatable low
variance and agreement with a shared external microphone recording.

No microphone permission or automatic calibration is enabled yet. There are
no acoustic measurements available, so `calibrationConfidence` is only
`System estimate` or `Manual adjustment`, never `Measured`. This prevents an
unreliable experimental measurement from silently altering playback.

## Hardware matrix pending

| Host output | iPhone output | Measured latency | Sync error | Status |
| --- | --- | --- | --- | --- |
| built-in | built-in | unavailable | unavailable | NOT TESTED — HARDWARE REQUIRED |
| built-in | wired | unavailable | unavailable | NOT TESTED — HARDWARE REQUIRED |
| built-in | Bluetooth | unavailable | unavailable | NOT TESTED — HARDWARE REQUIRED |
| Bluetooth | built-in | unavailable | unavailable | NOT TESTED — HARDWARE REQUIRED |
| Bluetooth | Bluetooth | unavailable | unavailable | NOT TESTED — HARDWARE REQUIRED |
| wired | Bluetooth | unavailable | unavailable | NOT TESTED — HARDWARE REQUIRED |

The user plans to run these physical tests at the end of the approved phases.
