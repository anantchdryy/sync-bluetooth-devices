import SwiftUI

struct ContentView: View {
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var model = ReceiverViewModel()

    var body: some View {
        NavigationStack {
            Form {
                Section("Desktop host") {
                    TextField("IPv4 address, e.g. 192.168.1.10", text: $model.hostIP)
                        .keyboardType(.numbersAndPunctuation)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .disabled(model.isListening)
                    TextField("Audio UDP port", text: $model.portText)
                        .keyboardType(.numberPad)
                        .disabled(model.isListening)
                    Button(model.isListening ? "Stop Listening" : "Start Listening") {
                        model.isListening ? model.stop() : model.start()
                    }
                }

                Section("Connection") {
                    LabeledContent("State", value: model.snapshot.status)
                    Text("Keep this app open while the desktop host streams to your iPhone's Wi-Fi address.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }

                Section("Packets") {
                    LabeledContent("Sequence", value: model.snapshot.lastSequenceNumber.map(String.init) ?? "—")
                    LabeledContent("Packets/sec", value: String(model.snapshot.packetsPerSecond))
                    LabeledContent("Received", value: String(model.snapshot.packetsReceived))
                    LabeledContent("Estimated loss", value: String(model.snapshot.packetsLost))
                }

                Section("Stream") {
                    LabeledContent("Sample rate", value: model.snapshot.sampleRate.map { "\($0) Hz" } ?? "—")
                    LabeledContent("Channels", value: model.snapshot.channels.map(String.init) ?? "—")
                    LabeledContent("Receive buffer", value: String(format: "%.1f ms", model.snapshot.bufferDepthMilliseconds))
                    LabeledContent("PCM format", value: model.snapshot.sampleRate == nil ? "—" :
                        (model.snapshot.audioFormatSupported ? "16-bit supported" : "Unavailable"))
                }
            }
            .navigationTitle("SyncAudio Receiver")
        }
        .onChange(of: scenePhase) { phase in
            if phase != .active && model.isListening {
                model.stop()
            }
        }
    }
}
