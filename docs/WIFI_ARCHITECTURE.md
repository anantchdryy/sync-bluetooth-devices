# Wi-Fi architecture

Wi-Fi/LAN is the device synchronization transport. Bluetooth is an optional
audio output route selected by each operating system; it is never used as the
device-to-device synchronization link.

## Responsibilities

| Role | Desktop | iPhone | Port |
| --- | --- | --- | --- |
| Discovery | DNS-SD/mDNS advertiser | Bonjour browser | multicast UDP 5353 |
| Control | bounded line-oriented TCP server | TCP client | TCP 40102 |
| Real-time audio | MTU-bounded UDP sender | UDP listener | UDP 40100 |
| Clock sync | `SCLK` responder | repeated `SCLK` probes | UDP 40101 |

`DiscoveryService`, `ControlServer`, `UdpAudioSender`, and `ClockSyncServer`
have separate sockets and threads. On iOS, `HostDiscovery`,
`HostControlChannel`, `UDPStreamReceiver`, and `HostClockSync` have separate
Network.framework objects. The audio player performs no network I/O.

The host advertises `_tandemaudio._tcp.local.` with the TCP control port. The
iPhone shows discovered hosts in its development UI. Selecting one connects
over TCP, receives its session metadata and IPv4 address, then starts UDP audio
and clock probes. Manual IPv4 and port entry remains available. Discovery is
local-subnet only and may be blocked by client isolation, VPNs, or firewalls.
If mDNS cannot start, the desktop host reports it and manual entry still works.

## Control channel

Commands and responses are ASCII lines, each at most 256 bytes. The client
sends `JOIN <device-id>`; the server responds with
`WELCOME <session-id> <stream-id> <audio-port> <clock-port> <sample-rate>
<channels> <host-ipv4>`. `HOST_STATE` returns the current playing state,
session ID, and most recently transmitted frame. `CLIENT_STATE` is acknowledged
and `LEAVE` closes the session. `PLAY`, `PAUSE`, and `SEEK` currently return an
explicit unsupported-command response. The channel is not an audio fallback.
The current host sends to a configured IPv4 destination or LAN broadcast; a
JOIN does not yet create a per-client unicast stream.

## Audio packet

`SAUD` version 2 uses a 52-byte header and at most 1,148 bytes of interleaved
PCM payload. The datagram ceiling is 1,200 bytes. Header integers use network
byte order; PCM samples are signed 16-bit little-endian. The full field table
is in the root README. Version, header length, payload length, channels,
sample rate, frame count, stream ID, frame arithmetic, and timestamp range are
validated before use. This version is a coordinated protocol change: old
version-1 clients must be updated. At 48 kHz stereo, 1,200 bytes holds at most
287 frames, about 5.98 ms. Ten to twenty milliseconds would exceed the
unfragmented packet budget for stereo PCM, so this prototype uses shorter
packets. No oversized UDP datagrams are intentionally sent.

## Clock, jitter, and scheduling

Clock probes use four monotonic timestamps (`t1` client send, `t2` host
receive, `t3` host send, `t4` client receive). Offset is
`((t2-t1)+(t3-t4))/2`; RTT is `(t4-t1)-(t3-t2)`. The iPhone probes repeatedly,
selects the median offset of the three lowest-RTT recent responses, and
reports RTT, quality, and a drift slope when sufficient time has elapsed.
Asymmetric paths can bias this estimate and cannot be detected from four
timestamps alone.

Audio follows: deserialize → reorder/jitter queue → translate host timestamp
to local monotonic time → schedule AVAudioPlayerNode → device output. It never
plays immediately on packet arrival. The iPhone tracks arrival jitter and
adjusts its target queue from 120 to 350 ms; increases are faster than
decreases. Missing frames are concealed with silence. The desktop client uses
the portable jitter buffer and gradual drift correction. Queue metrics do not
measure physical speaker latency.

## Reconnection and performance limits

The Phase 10 control socket reports disconnects but does not yet automatically
rejoin a changed host session. Phase 11 defines the reconnection state machine.
Host IP changes require rediscovery. Audio callbacks do not perform socket or
file I/O, but the current desktop jitter-buffer read takes a mutex and the
desktop host's local playback callback decodes WAV data. These are known
real-time risks to address with prefilled rings and hardware profiling. A
six-second, 48 kHz mono WAV streamed to Windows loopback on the development
machine sent 600 packets and 607,200 audio datagram bytes at 979.11 kbps
excluding IP/UDP headers. The process used 0.094 CPU-seconds over 7.633
wall-seconds (1.23% of one core on average) and peaked at 12.85 MiB working
set. This is a local host sample, not an iPhone Wi-Fi or acoustic result.
Phase 9 did not record an equivalent baseline, so a sync-performance
comparison is unavailable rather than inferred.
