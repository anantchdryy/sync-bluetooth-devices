# Known limitations

- Physical iPhone audio, acoustic synchronization, Bluetooth behavior, background playback, route changes, and reconnect time have not yet been measured on a device. The approximately 20 ms target is unverified.
- The Windows UI hosts one WAV file per room and a single room per process. There is no system-audio capture, playlist, streaming codec, or Android client.
- Automatic room discovery uses local mDNS; guest Wi-Fi isolation, VPNs, multicast filtering, or firewall policy can prevent discovery even when direct IPv4 works.
- The host's connection-quality labels are based on reported packet loss and connection state. They are coarse UI indicators, not acoustic proof of synchronization.
- Manual output delay can correct a measured constant route offset, but Bluetooth route delay may vary. No automatic microphone calibration is implemented.
- The desktop app currently supports Windows; the C++ core remains portable, but macOS host integration is unvalidated.
- The iPhone archive and signed device build require a Mac, Xcode, and user-managed signing. Only simulator build/tests are available in CI.
- The Windows installer has not been exercised on an independent clean computer in this environment.
