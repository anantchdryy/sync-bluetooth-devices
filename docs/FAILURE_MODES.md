# Failure modes and recovery

The table separates implemented recovery from procedures that still require
hardware validation. `Reconnect` means the iPhone's TCP control channel retries
at 1, 2, 4, then 8 seconds, resets audio and clock state, rejoins, and buffers
fresh audio. The host does not stop because a client fails. No client can
recover audio if the host itself has stopped broadcasting.

| Failure | Cause | Symptom | Detection | Automatic recovery | User-visible recovery | Telemetry | Test procedure |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Latency spike | Queued network traffic | late PCM | RTT/jitter/depth rise | adaptive buffer grows | shows degraded | RTT, late, buffer | inject 250 ms delay |
| Jitter | Variable transit | irregular arrivals | arrival EWMA | buffer target rises slowly | shows degraded if starved | jitter, depth | inject 5–100 ms jitter |
| Packet loss | Wireless errors | brief silence | sequence gap | conceal lost frames | none unless persistent | loss, concealed | inject 1–10% loss |
| Burst loss | Consecutive misses | audible gap | depth reaches zero | rebuffer | buffering | underruns, depth | inject 100–1000 ms blackout |
| Duplication | Network resend | duplicate PCM | sequence set | discard duplicate | none | duplicate/late | inject 100% duplication |
| Reordering | Different queue delays | gaps then late packets | sequence and frame position | reorder within buffer | none unless late | out-of-order, late | inject reorder probability |
| Temporary disconnect | Wi-Fi interruption | stream pauses | control EOF, no packets | reconnect, remeasure, rebuffer | reconnecting | state, retry count | disable Wi-Fi for 2 s |
| Complete disconnect | Host disappears | silence | control failure | retry until host returns | reconnecting | state, last packet | stop/restart host |
| Weak Wi-Fi | low signal | loss/jitter | loss and jitter | larger buffer, rebuffer | unstable | RSSI unavailable; loss/jitter | test distant room |
| Congested Wi-Fi | competing traffic | delayed PCM | RTT/jitter | buffer adaptation | unstable | RTT/jitter/loss | run competing transfer |
| Host IP/DHCP change | lease or network switch | old endpoint fails | TCP/UDP timeout | Bonjour service endpoint can resolve again; manual IPv4 requires reentry | reconnecting | endpoint, state | change host address |
| Firewall | blocked ports | cannot join or no audio | control failure/zero packets | retry; no firewall bypass | check firewall message | control status, packet count | block TCP/UDP ports |
| VPN | altered routes | discovery or stream missing | browser/connection errors | retry | use same LAN | discovery/control status | enable VPN |
| Client isolation | router policy | device peers unreachable | discovery absent | none | disable isolation | discovery count | enable guest isolation |
| Different subnets | no mDNS forwarding | room missing | discovery absent | manual IPv4 if routed | enter host IP | discovery status | move phone to another subnet |
| Wi-Fi power saving | radio sleep | spikes | packet gaps | buffer/reconnect | unstable | jitter/loss | lock phone/idle |
| Switching Wi-Fi | interface migration | endpoint lost | control state | retry Bonjour endpoint | reconnecting | state, endpoint | switch access points |
| Mobile hotspot | NAT/client policy | discovery absent | browser status | manual address if routed | join manually | discovery/control | test hotspot |
| Underrun | queue empty | audible gap | player counter | stop/rebuffer | buffering | underruns | inject long blackout |
| Buffer overrun | sender faster/paused output | stale PCM | bounded queue limits | evict stale packets | degraded if persistent | depth, overflow | inject duplicates/delay |
| Sample-rate mismatch | stream format changes | wrong pitch or reject | header/stream validation | reset on new session; reject midstream | reconnect | sample rate, format | mutate sample-rate field |
| Output device change | route switched | timing changes | iOS route notification | iPhone loads new route estimate and re-buffers | calibrating | route ID, latency | swap outputs |
| Headphones plug/unplug | route switched | possible gap | iOS route notification | iPhone re-buffers with route-specific adjustment | calibrating | route ID | insert/remove wired set |
| Bluetooth route change | OS reconnect | latency jump | iOS route notification | iPhone re-buffers; acoustic accuracy pending | calibrating | route/latency | connect/disconnect headset |
| Audio interruption | call/audio focus | playback halts | AVAudioSession notification | stop engine and rebuffer on end | waiting for audio | state, underruns | simulate call/Siri |
| CPU overload | callback misses | stutter | underrun count | rebuffer | unstable | CPU external, underruns | synthetic CPU load |
| Laptop sleep/wake | host clock/network stop | stream disappears | control and packet timeout | clients retry; host restart may be required | reconnecting | state, session | sleep/wake laptop |
| Phone lock/unlock | lifecycle/audio policy | playback stops | scene phase/interruption | app stops in background today; foreground requires user restart | Start Listening | lifecycle, state | lock/unlock phone |
| iOS background/foreground | app suspended | no stream | scene phase | current app stops in background | start again in foreground | state | background 30 s |
| Local Network denied | permission | no discovery/socket | NWBrowser/NWConnection error | cannot auto-grant | enable in Settings | discovery/control error | deny permission |
| Incoming call/focus interruption | OS session | audio pauses | interruption notification | engine reset after interruption | buffering | interruption state | place call |
| Bad clock sample | queue asymmetry/outlier | offset jump | RTT and residual filter | low-RTT median, regression outlier reject | quality Fair | RTT, offset, quality | inject clock outlier |
| Asymmetric latency | paths differ | biased sync | cannot infer from 4 timestamps alone | none; calibration needed | manual calibration | RTT, offset, sync estimate | delay one direction |
| Clock drift | different oscillators | gradual separation | regression slope | bounded ratio correction on desktop | none | drift ppm, ratio | simulate ±10…500 ppm |
| Sudden clock offset | clock source change | scheduler skew | repeated residual outliers | reset estimator after 3; iOS rejoin still needs validation | syncing | offset, quality | inject persistent 30 ms step |
| Host pause/resume | transport lacks scheduled commands | playback inconsistent | control state | not implemented (Phase 13) | restart stream | playback state | pause host |
| Seek | transport lacks scheduled seek | wrong position | stream position discontinuity | not implemented (Phase 13) | restart stream | frame, session | seek host |
| Mid-song join | client joins late | misses beginning | WELCOME/current UDP | discard expired frames; buffer current | buffering | host frame, depth | join at 50% |
| Mid-song reconnect | transport returns | current position changed | fresh WELCOME/session | reset clock/queue, rebuffer live packets | reconnecting | session/frame | interrupt Wi-Fi mid-song |
| Host restart | new session ID | stale packets | session/stream validation | reset and rejoin | reconnecting | session ID | restart host |
| Client restart | app process exits | detached client | new JOIN | fresh session join | joining | device/session | force-close/relaunch |

## Current limits

The retry path is implemented in the iPhone control channel, but physical
Wi-Fi handoff, lock/background behavior, and acoustic recovery time have not
been measured. TCP JOIN currently accepts any LAN client with a syntactically
valid device ID; room authorization belongs to Phase 13. Windows output-route
changes reported by miniaudio stop the stream and require a restart with a
route-appropriate calibration; automatic desktop rebuffering is not
implemented. Some backends do not report reroutes.
