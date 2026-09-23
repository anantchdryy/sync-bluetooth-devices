# iPhone packet receiver and playback

`SyncAudioReceiver.xcodeproj` is an iPhone app with a small SwiftUI screen. It
uses Network.framework to listen for the desktop host's `SAUD` UDP packets and
AVAudioEngine to play signed 16-bit PCM. The player converts interleaved PCM to
floating point, reorders packets by frame index, conceals missing frames with
silence, and starts after about 180 ms has accumulated. It targets about 250 ms
of scheduled audio. The receive buffer retains up to about 500 ms of PCM for
statistics; its displayed depth is separate from queued playback audio.

## Xcode and device setup

1. On a Mac with Xcode installed, open `ios/SyncAudioReceiver.xcodeproj`.
2. Select the **SyncAudioReceiver** target, open **Signing & Capabilities**, and
   choose your Apple development team. If Xcode reports a bundle identifier
   conflict, change `com.anantchdryy.SyncAudioReceiver` to an identifier owned
   by your team. Leave **Automatically manage signing** enabled.
3. Connect an iPhone, trust the Mac if asked, select the phone as the run
   destination, and press **Run**. Xcode may ask you to enable Developer Mode on
   the phone. The project targets iOS 16 or later.
4. When you tap **Start Listening**, allow the local-network permission prompt.
   The purpose string is in `SyncAudioReceiver/Info.plist`. If permission was
   previously denied, enable it in iPhone **Settings → Privacy & Security →
   Local Network**.

The shared scheme includes unit tests. On a Mac, select an iPhone simulator and
run **Product → Test**, or run:

```sh
xcodebuild -project ios/SyncAudioReceiver.xcodeproj \
  -scheme SyncAudioReceiver \
  -destination 'platform=iOS Simulator,name=iPhone 16' \
  CODE_SIGNING_ALLOWED=NO test
```

Choose an installed simulator name if `iPhone 16` is unavailable. A physical
iPhone is required to validate desktop-to-phone Wi-Fi delivery.

## Receive from the desktop

1. Connect the desktop and iPhone to the same Wi-Fi/LAN. Find the desktop's
   IPv4 address and the iPhone's IPv4 address in their network settings.
2. In the iPhone app, enter the **desktop host IPv4 address** and audio UDP port
   `40100`, then tap **Start Listening**. Leave the app in the foreground.
3. On the desktop, build the repository as described in the root README. Run
   the host with the **iPhone's IPv4 address** as its destination:

   ```powershell
   .\build\Release\syncaudio.exe host "C:\path\to\file.wav" --address 192.168.1.20 --port 40100
   ```

   Replace `192.168.1.20` with the iPhone's address. Permit private-network
   traffic through the desktop firewall if prompted.
4. The app changes from **Waiting for host packets** to **Receiving** and shows
   the latest sequence number, packets per second, estimated packet loss,
   sample rate, channel count, receive buffer depth, playback queue depth,
   underruns, concealed frames, and device output pipeline latency. Audio begins
   when the playback state reads **Playing**. Tap **Stop Listening**
   to release the UDP socket. The app also stops listening when backgrounded.

The host IP field filters incoming datagrams; UDP has no persistent connection.
If the app stays on **Waiting for host packets**, check both IP addresses, the
port, local-network permission, firewall, and whether the host command is
running. Start the phone listener before starting the host so it sees the
beginning of the stream.

## Playback latency and validation

The startup queue target is 180 ms, and the steady scheduled queue target is
250 ms. The app reports `outputLatency + ioBufferDuration` from AVAudioSession
as **Output pipeline**; that value excludes speaker or Bluetooth accessory
delay. Packet transit time and scheduling also contribute to end-to-end
latency. These are design targets and API values, not measured acoustic latency.
The iOS Simulator build and converter/queue tests verify software paths. To
verify actual audio, run on an iPhone, play an audible WAV on the desktop host,
and listen on the phone. Record the playback state, underrun count, output
pipeline value, and whether audio is uninterrupted for at least one minute.
