# macOS port plan

The portable C++ `core/` library can be shared with a macOS target. The CLI currently builds with CMake on supported Unix toolchains, but a macOS host/product build has not been validated in this phase.

A complete macOS app needs SwiftUI or AppKit shell screens, Core Audio output and device-route observation, Bonjour service discovery/advertising, LAN sockets, app signing, and packaging. The existing C++ room control, packet format, clock synchronization, jitter and drift algorithms should be retained. Validate sample clock and actual speaker/Bluetooth latency on Mac hardware before claiming synchronization with iPhone or Windows.
