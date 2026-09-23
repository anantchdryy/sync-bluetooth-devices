# Android port plan

The platform-neutral C++ `core/` library contains packet serialization, clock synchronization, jitter buffering, drift correction, and latency arithmetic. Android can link this through JNI/CMake without importing Windows desktop audio or Swift UI code.

An Android client still needs a Kotlin/Compose interface, LAN discovery through Android NSD or mDNS, TCP room control and UDP audio/clock sockets, lifecycle and Wi-Fi permission handling, and an audio output layer using Oboe/AAudio or AudioTrack. It must map the host presentation timestamps to a stable Android clock, measure or estimate route latency, and handle Bluetooth route transitions. Port the existing protocol fixtures first, then run on-device audio and acoustic tests. No Android implementation or measured result is claimed.
