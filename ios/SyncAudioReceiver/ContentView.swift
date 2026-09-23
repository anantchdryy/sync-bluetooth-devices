import AVKit
import SwiftUI

struct ContentView: View {
    @StateObject private var model = ReceiverViewModel()
    @AppStorage("TandemAudio.onboardingComplete") private var onboardingComplete = false
    @AppStorage("TandemAudio.developerMode") private var developerMode = false

    var body: some View {
        if onboardingComplete {
            TabView {
                home.tabItem { Label("Home", systemImage: "house") }
                room.tabItem { Label("Room", systemImage: "music.note.house") }
                devices.tabItem { Label("Device", systemImage: "hifispeaker") }
                settings.tabItem { Label("Settings", systemImage: "gearshape") }
            }
            .tint(.mint)
        } else {
            onboarding
        }
    }

    private var onboarding: some View {
        VStack(alignment: .leading, spacing: 20) {
            Image(systemName: "waveform.path")
                .font(.system(size: 58))
                .foregroundStyle(.mint)
            Text("Play together")
                .font(.largeTitle.bold())
            Text("Turn nearby phones and computers into speakers for the same music.")
                .font(.title3)
            VStack(alignment: .leading, spacing: 12) {
                Label("Connect devices to the same Wi-Fi.", systemImage: "wifi")
                Label("Create a room on your computer.", systemImage: "plus.circle")
                Label("Join the room here and choose your speaker.", systemImage: "hifispeaker")
            }
            .padding(.vertical, 12)
            Spacer()
            Button("Find rooms") { onboardingComplete = true }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .frame(maxWidth: .infinity)
        }
        .padding(30)
    }

    private var home: some View {
        NavigationStack {
            Form {
                Section {
                    if model.discoveredHosts.isEmpty {
                        VStack(alignment: .leading, spacing: 8) {
                            Text("No nearby rooms yet").font(.headline)
                            Text("Open Tandem Audio on a computer and create a room. Keep both devices on the same Wi-Fi.")
                                .foregroundStyle(.secondary)
                            Text(model.discoveryStatus)
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                        .padding(.vertical, 8)
                    } else {
                        ForEach(model.discoveredHosts) { host in
                            Button {
                                model.connect(to: host)
                            } label: {
                                HStack {
                                    Image(systemName: "music.note.house")
                                        .foregroundStyle(.mint)
                                    Text(host.name)
                                    Spacer()
                                    Text("Join").foregroundStyle(.secondary)
                                }
                            }
                            .disabled(model.isListening)
                        }
                    }
                } header: { Text("Nearby rooms") }

                if model.isListening {
                    Section("Current room") {
                        LabeledContent("Room", value: model.roomName.isEmpty ? "Joining…" : model.roomName)
                        LabeledContent("Status", value: model.userStatus)
                        Button("Leave room", role: .destructive) { model.stop() }
                    }
                }
                Section {
                    Text("If rooms do not appear, allow Local Network access in iPhone Settings and check that the computer and iPhone are on the same network.")
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("Tandem Audio")
        }
    }

    private var room: some View {
        NavigationStack {
            Form {
                Section {
                    VStack(alignment: .leading, spacing: 8) {
                        Text(model.roomName.isEmpty ? "No room open" : model.roomName)
                            .font(.title2.bold())
                        Label(model.userStatus, systemImage: model.connectionState == .playing
                              ? "waveform" : "circle.dotted")
                            .foregroundColor(model.connectionState == .playing ? .mint : .gray)
                    }
                    .padding(.vertical, 8)
                    if model.isListening {
                        LabeledContent("Host", value: model.hostPlaybackState.capitalized)
                        Button("Leave room", role: .destructive) { model.stop() }
                    } else {
                        Text("Choose a nearby room on Home to start listening.")
                            .foregroundStyle(.secondary)
                    }
                }
                if model.isListening {
                    Section("Audio output") {
                        LabeledContent("Playing through", value: model.playback.outputRouteType)
                        HStack {
                            Text("Choose output")
                            Spacer()
                            AudioRoutePicker()
                                .frame(width: 44, height: 44)
                        }
                        Text("Changing speakers may briefly rebuffer audio while Tandem Audio recalibrates the route.")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle("Room")
        }
    }

    private var devices: some View {
        NavigationStack {
            Form {
                Section("This iPhone") {
                    LabeledContent("Connection", value: model.userStatus)
                    LabeledContent("Quality", value: model.connectionQuality)
                    LabeledContent("Speaker", value: model.playback.outputRouteType)
                    if model.playback.state == "Output changed; buffering" {
                        Label("Your audio output changed. Recalibrating…", systemImage: "arrow.triangle.2.circlepath")
                    }
                }
                Section {
                    Text("Each device synchronizes with the room host independently. Other members are visible in the desktop app.")
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("Device")
        }
    }

    private var settings: some View {
        NavigationStack {
            Form {
                Section("Audio output") {
                    LabeledContent("Current route", value: model.playback.outputRouteType)
                    HStack {
                        Text("Choose speaker")
                        Spacer()
                        AudioRoutePicker().frame(width: 44, height: 44)
                    }
                }
                Section("Calibration") {
                    LabeledContent("Status", value: model.playback.calibrationConfidence)
                    Text("Tandem Audio uses the iPhone's output timing estimate. For best alignment, compare a short click from both speakers.")
                        .foregroundStyle(.secondary)
                    DisclosureGroup("Advanced manual adjustment") {
                        Slider(value: $model.calibrationAdjustmentMs,
                               in: -1_000...1_000, step: 1) { editing in
                            if !editing { model.applyCalibration() }
                        }
                        LabeledContent("Adjustment", value:
                            String(format: "%+.0f ms", model.calibrationAdjustmentMs))
                    }
                }
                Section("Diagnostics") {
                    Toggle("Developer diagnostics", isOn: $developerMode)
                    Button("Prepare diagnostics export") { model.exportDiagnostics() }
                    if let url = model.diagnosticsURL {
                        ShareLink("Share diagnostics JSON", item: url)
                    }
                }
                if developerMode {
                    developerDiagnostics
                }
            }
            .navigationTitle("Settings")
        }
    }

    private var developerDiagnostics: some View {
        Group {
            Section("Manual connection") {
                TextField("Host IPv4 address", text: $model.hostIP)
                    .keyboardType(.numbersAndPunctuation)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                TextField("Audio UDP port", text: $model.portText).keyboardType(.numberPad)
                TextField("Clock UDP port", text: $model.controlPortText).keyboardType(.numberPad)
                TextField("Session TCP port", text: $model.sessionPortText).keyboardType(.numberPad)
                Button("Connect to host") { model.start() }.disabled(model.isListening)
            }
            Section("Network and playback") {
                LabeledContent("Control", value: model.controlStatus)
                LabeledContent("Packets/sec", value: String(model.snapshot.packetsPerSecond))
                LabeledContent("Packet loss", value: String(format: "%.2f%%", model.snapshot.packetLossPercent))
                LabeledContent("RTT", value: model.clockEstimate.map {
                    String(format: "%.3f ms", $0.roundTripMilliseconds)
                } ?? "Unavailable")
                LabeledContent("Host offset", value: model.clockEstimate.map {
                    String(format: "%.3f ms", $0.offsetMilliseconds)
                } ?? "Unavailable")
                LabeledContent("Buffer", value: String(format: "%.1f ms", model.playback.queuedMilliseconds))
                LabeledContent("Underruns", value: String(model.playback.underruns))
                LabeledContent("Sync estimate", value: model.playback.estimatedSyncErrorMilliseconds.map {
                    String(format: "%+.3f ms", $0)
                } ?? "Unavailable")
                Text("The sync estimate is based on render timing and system output latency. It is not an acoustic measurement.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
    }
}

private struct AudioRoutePicker: UIViewRepresentable {
    func makeUIView(context: Context) -> AVRoutePickerView {
        let picker = AVRoutePickerView()
        picker.activeTintColor = .systemMint
        picker.tintColor = .systemMint
        return picker
    }

    func updateUIView(_ uiView: AVRoutePickerView, context: Context) { }
}
