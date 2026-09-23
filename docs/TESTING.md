# Testing Tandem Audio

## Equipment

Required for a physical iPhone test: a Windows 10/11 PC, iPhone with iOS 16 or newer, both on the same ordinary LAN/Wi-Fi, one audible WAV file, and temporary access to a Mac running a compatible Xcode version to sign and install the iPhone app. A free Apple Account can be used for personal device testing, with periodic reprovisioning. A borrowed Mac is the most direct route; a hosted Mac is useful for builds but may not support attaching your physical iPhone. A macOS VM can test compilation if it runs Xcode, but virtual networking and audio do not establish physical synchronization accuracy. The iPhone app cannot be installed directly from the Windows installer.

Optional: a second Windows computer, wired iPhone output, Bluetooth speaker or headphones, a phone/camera that records both speakers at once, and a router where client isolation can be disabled. A second recorder or independent microphone is necessary for acoustic measurement; app diagnostics alone estimate timing.

## Software checks

Run Debug and Release CTest targets using [building](BUILDING.md). The `network_impairment_tests` and `reliability_stress_tests` cover seeded loss, delay, jitter, blackout, reordering, drift, malformed packets, and bounds. GitHub's Windows and iPhone workflows compile and test the pushed tree. A simulator verifies iOS code paths but not physical routes or speaker timing.

## First installation and room workflow

1. Install the Windows build and open **Tandem Audio** from Start. Choose an ordinary PCM WAV file, name a room, and select **Create Room**. Permit private-network access if prompted.
2. Install the iPhone app through Xcode and launch it. Allow Local Network access. Choose the room under nearby rooms and tap **Join**. If it is absent, verify same subnet, multicast/mDNS, VPN, and firewall; use Developer Diagnostics manual IP to isolate discovery from transport failures.
3. Confirm the room lists the iPhone and that it receives audio. Test play, pause, resume, seek, join while playing, leave, and rejoin. Record any interruption or unexpected buffer reset.
4. Lock the phone, background and foreground it, change from built-in output to wired and Bluetooth where available, briefly disconnect Wi-Fi, and reconnect. Capture status and elapsed recovery time.
5. Export iPhone diagnostics JSON and Windows logs from `%LOCALAPPDATA%\TandemAudio\Logs`. Record the exact device models, OS versions, output routes, Wi-Fi layout, and approximate room distance.
6. For acoustic sync, play a WAV with spaced sharp clicks, record both physical speakers on a *single* recorder, and compare the same click peaks in the waveform. Repeat with each output route and after route changes. Record both individual offsets and variation over time. Do not label the app's estimated sync error as measured acoustic error.

Run installer and uninstaller on a clean Windows environment or fresh Windows account when available; verify Start Menu entry, room creation, and removal. The installer does not automatically change Windows Firewall rules.
