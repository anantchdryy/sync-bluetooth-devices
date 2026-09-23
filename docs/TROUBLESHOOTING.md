# Troubleshooting

**No nearby room.** Put the desktop and iPhone on the same non-isolated Wi-Fi/LAN. Disable VPN temporarily. Allow Tandem Audio on Windows Private networks; check that multicast DNS UDP 5353 and TCP 40102 are permitted. On iPhone, enable Tandem Audio in Settings > Privacy & Security > Local Network. Developer Diagnostics manual IP can distinguish discovery failure from direct transport failure.

**Room visible but no audio.** Verify the WAV file is PCM and the host is still playing. Check the iPhone output route and volume, UDP 40100 and 40101, the desktop firewall, and packet/underrun counters in Developer Diagnostics. Try built-in speakers before Bluetooth to isolate route latency.

**Reconnecting or unstable.** Keep both devices on the same Wi-Fi band or nearby access points in one LAN, avoid guest/client isolation, then capture diagnostics. Brief gaps may cause concealment/underruns; repeated stalls need the packet-loss and RTT log values. The Windows engine log is in `%LOCALAPPDATA%\TandemAudio\Logs`.

**Noticeable echo.** Play a WAV with short clicks and record both speakers together. Check the output pipeline estimate and route-specific manual delay. Bluetooth accessory delay can be large or variable. Report the actual recorded click separation; app estimated sync error is not an acoustic measurement.

**iPhone cannot install.** Use a Mac running a compatible Xcode version, sign into an Apple Account, choose a development team and unique bundle ID, select the physical iPhone as destination, trust the computer, and enable Developer Mode if prompted. A simulator build from GitHub Actions is not a signed installable device app.
