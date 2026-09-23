import SwiftUI

struct ContentView: View {
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var model = ReceiverViewModel()

    var body: some View {
        NavigationStack {
            Form {
                Section("Nearby hosts") {
                    LabeledContent("Discovery", value: model.discoveryStatus)
                    if model.discoveredHosts.isEmpty {
                        Text("No Tandem Audio host found yet")
                            .foregroundStyle(.secondary)
                    }
                    ForEach(model.discoveredHosts) { host in
                        Button("Join \(host.name)") { model.connect(to: host) }
                            .disabled(model.isListening)
                    }
                }
                Section("Desktop host") {
                    TextField("IPv4 address, e.g. 192.168.1.10", text: $model.hostIP)
                        .keyboardType(.numbersAndPunctuation)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .disabled(model.isListening)
                    TextField("Audio UDP port", text: $model.portText)
                        .keyboardType(.numberPad)
                        .disabled(model.isListening)
                    TextField("Clock UDP port", text: $model.controlPortText)
                        .keyboardType(.numberPad)
                        .disabled(model.isListening)
                    TextField("Session TCP port", text: $model.sessionPortText)
                        .keyboardType(.numberPad)
                        .disabled(model.isListening)
                    Button(model.isListening ? "Stop Listening" : "Start Listening") {
                        model.isListening ? model.stop() : model.start()
                    }
                }

                Section("Connection") {
                    LabeledContent("State", value: model.snapshot.status)
                    LabeledContent("Control", value: model.controlStatus)
                    Text("Join a nearby host, or enter its address for debugging. Keep this app open while streaming.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }

                Section("Packets") {
                    LabeledContent("Sequence", value: model.snapshot.lastSequenceNumber.map { String($0) } ?? "—")
                    LabeledContent("Packets/sec", value: String(model.snapshot.packetsPerSecond))
                    LabeledContent("Received", value: String(model.snapshot.packetsReceived))
                    LabeledContent("Estimated loss", value: String(model.snapshot.packetsLost))
                    LabeledContent("Packet loss", value: String(format: "%.2f%%", model.snapshot.packetLossPercent))
                    LabeledContent("Network jitter", value: String(format: "%.1f ms", model.snapshot.networkJitterMilliseconds))
                }

                Section("Stream") {
                    LabeledContent("Sample rate", value: model.snapshot.sampleRate.map { "\($0) Hz" } ?? "—")
                    LabeledContent("Channels", value: model.snapshot.channels.map { String($0) } ?? "—")
                    LabeledContent("Receive buffer", value: String(format: "%.1f ms", model.snapshot.bufferDepthMilliseconds))
                    LabeledContent("PCM format", value: model.snapshot.sampleRate == nil ? "—" :
                        (model.snapshot.audioFormatSupported ? "16-bit supported" : "Unavailable"))
                }

                Section("Playback") {
                    LabeledContent("State", value: model.playback.state)
                    LabeledContent("Queued audio", value: String(format: "%.1f ms", model.playback.queuedMilliseconds))
                    LabeledContent("Target buffer", value: String(format: "%.1f ms", model.playback.targetBufferMilliseconds))
                    LabeledContent("Late packets", value: String(model.playback.latePackets))
                    LabeledContent("Late rate", value: String(format: "%.2f%%", model.playback.latePacketRate * 100))
                    LabeledContent("Underruns", value: String(model.playback.underruns))
                    LabeledContent("Concealed frames", value: String(model.playback.concealedFrames))
                    LabeledContent("Output pipeline", value: model.playback.outputLatencyMilliseconds.map {
                        String(format: "%.1f ms", $0)
                    } ?? "—")
                    LabeledContent("Device sample rate", value: model.playback.hardwareSampleRate.map {
                        String(format: "%.0f Hz", $0)
                    } ?? "—")
                }

                Section("Synchronization") {
                    LabeledContent("Clock", value: model.clockStatus)
                    LabeledContent("Host offset", value: model.clockEstimate.map {
                        String(format: "%.3f ms", $0.offsetMilliseconds)
                    } ?? "—")
                    LabeledContent("RTT", value: model.clockEstimate.map {
                        String(format: "%.3f ms", $0.roundTripMilliseconds)
                    } ?? "—")
                    LabeledContent("Clock samples", value: model.clockEstimate.map {
                        String($0.sampleCount)
                    } ?? "0")
                    LabeledContent("Clock quality", value: model.clockEstimate?.measurementQuality ?? "—")
                    LabeledContent("Estimated drift", value: model.clockEstimate?.estimatedDriftPpm.map {
                        String(format: "%+.1f ppm", $0)
                    } ?? "—")
                    LabeledContent("Buffer", value: String(format: "%.1f ms", model.playback.queuedMilliseconds))
                    LabeledContent("Presentation delay", value: String(format: "%.1f ms", model.playback.presentationDelayMilliseconds))
                    LabeledContent("Estimated sync error", value: model.playback.estimatedSyncErrorMilliseconds.map {
                        String(format: "%+.3f ms", $0)
                    } ?? "—")
                    Text("Sync error is estimated from engine render timing and the audio session's output latency. It is not a measured acoustic difference.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("SyncAudio Receiver")
        }
        .onChange(of: scenePhase) { phase in
            if phase == .background && model.isListening {
                model.stop()
            }
        }
    }
}
