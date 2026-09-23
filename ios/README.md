# Tandem Audio for iPhone

The iPhone app uses SwiftUI, Network.framework, AVAudioSession, and AVAudioEngine. It discovers a nearby Windows room with Bonjour, joins through TCP, receives PCM UDP packets, estimates the host clock with `SCLK` probes, and schedules audio with a bounded jitter queue. Home, Room, Device, and Settings screens keep timing details under Developer Diagnostics.

## Build and install

On a Mac with a compatible Xcode version, open `SyncAudioReceiver.xcodeproj`, choose the `SyncAudioReceiver` target, select your Apple development team under Signing & Capabilities, and change `com.anantchdryy.SyncAudioReceiver` if that identifier is unavailable to your team. Connect and trust your iPhone, enable Developer Mode if prompted, select it as destination, and press Run. The project targets iOS 16 or later. A free Apple Account can sign for personal device testing, with periodic reprovisioning. See [building](../docs/BUILDING.md).

Allow Local Network access when prompted. If denied, turn it on in iPhone Settings > Privacy & Security > Local Network. The app does not use the microphone or manage Bluetooth pairing. It declares background audio and observes interruptions and output-route changes. A Bluetooth speaker must be paired and selected through iOS.

## Use

Connect the Windows PC and iPhone to the same non-isolated LAN. Start the Windows Tandem Audio app, choose a PCM WAV file, and create a room. On iPhone, complete the short first-run screen, tap the room under Nearby Rooms, and join. The Room screen shows playback and connection state; Device shows the output route; Settings has route adjustment and Developer Diagnostics. If discovery fails, Developer Diagnostics offers manual host IPv4 and ports. Defaults are audio UDP 40100, clock UDP 40101, and TCP session 40102.

The app starts playback only after it has a clock sample and enough queued audio. The startup queue is around 180 ms and adapts to network jitter. It reports packet counts, loss, buffer depth, output pipeline estimate, and estimated sync error under Developer Diagnostics. These are software estimates, not an acoustic measurement. Export diagnostics JSON after each test; see [testing](../docs/TESTING.md).

Changing the output route clears queued playback and re-buffers. The route-specific manual adjustment is stored by port UID and is not copied to a new route. This is manual compensation, not automatic microphone calibration. See [output latency](../docs/OUTPUT_LATENCY.md).

The shared Xcode scheme runs parser and playback logic tests in an iPhone simulator. The repository's GitHub Actions workflow builds and tests it. A physical iPhone is required to verify sound, Wi-Fi recovery, background playback, Bluetooth behavior, and acoustic alignment.
