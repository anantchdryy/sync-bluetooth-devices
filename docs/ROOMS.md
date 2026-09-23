# Rooms and multiple clients

The desktop creates one in-memory room for each host run. It generates a
128-bit room identifier, a session identifier, and a stream identifier. Bonjour
advertises the room name and identifier on the local network. Each iPhone
opens its own TCP control connection, receives `WELCOME`, `ROOM`, `SYNC_AT`,
and `HOST_STATE`, then runs its own clock probes and playback queue. A join or
leave does not restart the host stream or other members.

The host sends one timestamped PCM datagram by UDP unicast to each joined
member. This favors home-router compatibility over multicast efficiency. A
failed destination is counted and does not stop sends to healthy members.
The room tracks each member's reported RTT, jitter, loss, buffer depth,
output latency, clock offset, and estimated sync error separately. Health
reports are client estimates, not measurements of acoustic alignment.

The local host can send `PLAY`, `PAUSE`, `STOP`, or `SEEK <frame>` to TCP
port 40102 through `syncaudio control`. The server rejects those commands
from non-loopback IPv4 peers. Accepted commands are scheduled for a monotonic
host timestamp two seconds later and announced in `HOST_STATE` as `PENDING`.
Clients use their independently estimated host offset to act at that time.
Seek rotates the stream identifier to keep old packets out of the new segment.
This is LAN prototype authorization: any local process may issue controls.
There is no authentication or encryption for membership or audio yet.

```powershell
.\build\Release\syncaudio.exe host .\music.wav --room-name "Living Room"
.\build\Release\syncaudio.exe control PAUSE
.\build\Release\syncaudio.exe control PLAY
.\build\Release\syncaudio.exe control "SEEK 48000"
.\build\Release\syncaudio.exe control STOP
```

If the host disappears, clients enter `RECONNECTING` and attempt to rejoin.
Host election and migration are future work. The initial room size is capped
at 32 concurrent control connections; this is a safety bound, not a tested
number of synchronized hardware clients.

## Loopback scale test

`room_transport_tests` streams a 0.25-second, 8 kHz mono WAV to 1, 2, 5, and
10 UDP receivers on one Windows machine. Each receiver got all 25 expected
packets. A deliberately invalid eleventh destination failed 25 times while
the ten valid receivers still each got 25 packets. Results on the development
machine (process-wide CPU time and working set):

| Receivers | Delivered datagrams | Payload bytes | CPU seconds | Working set |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 25 | 5,300 | 0.241 | 6.83 MiB |
| 2 | 50 | 10,600 | 0.242 | 6.86 MiB |
| 5 | 125 | 26,500 | 0.242 | 6.88 MiB |
| 10 | 250 | 53,000 | 0.241 | 7.09 MiB |

The socket sender, room snapshot/copy, packet serialization, and network
capacity will limit scaling as client count rises. This short loopback run
does not establish a practical Wi-Fi client limit, clock quality, or acoustic
sync. Real iPhones, varied output routes, and Wi-Fi interference still need
testing at the end of the project.
