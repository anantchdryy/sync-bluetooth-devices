import Combine
import Foundation
import Network

enum ReceiverConnectionState: String {
    case disconnected = "DISCONNECTED"
    case discovering = "DISCOVERING"
    case connecting = "CONNECTING"
    case syncing = "SYNCING"
    case buffering = "BUFFERING"
    case playing = "PLAYING"
    case degraded = "DEGRADED"
    case reconnecting = "RECONNECTING"
}

@MainActor
final class ReceiverViewModel: ObservableObject {
    @Published var hostIP = ""
    @Published var portText = "40100"
    @Published var controlPortText = "40101"
    @Published var sessionPortText = "40102"
    @Published private(set) var discoveredHosts = [DiscoveredHost]()
    @Published private(set) var discoveryStatus = "Starting discovery"
    @Published private(set) var controlStatus = "Disconnected"
    @Published private(set) var snapshot = ReceiverSnapshot()
    @Published private(set) var playback = PlaybackSnapshot()
    @Published private(set) var clockEstimate: ClockEstimate?
    @Published private(set) var clockStatus = "Idle"
    @Published private(set) var isListening = false
    @Published private(set) var connectionState: ReceiverConnectionState = .discovering
    @Published private(set) var diagnosticsURL: URL?

    private let receiver = UDPStreamReceiver()
    private let audio = AudioPlaybackController()
    private let clock = HostClockSync()
    private let discovery = HostDiscovery()
    private let control = HostControlChannel()
    private var expectedSessionID: UInt64?

    init() {
        receiver.onSnapshot = { [weak self] snapshot in
            self?.audio.updateTargetBuffer(milliseconds: snapshot.targetBufferMilliseconds)
            DispatchQueue.main.async { [weak self] in
                self?.snapshot = snapshot
                if self?.connectionState == .playing && snapshot.status != "Receiving" {
                    self?.connectionState = .degraded
                }
            }
        }
        receiver.onPacket = { [weak self] packet in
            self?.audio.receive(packet)
        }
        audio.onSnapshot = { [weak self] playback in
            DispatchQueue.main.async { [weak self] in
                self?.playback = playback
                if playback.state == "Playing" { self?.connectionState = .playing }
                else if playback.state == "Buffering" { self?.connectionState = .buffering }
            }
        }
        clock.onEstimate = { [weak self] estimate in
            self?.audio.updateClock(estimate)
            DispatchQueue.main.async { [weak self] in
                self?.clockEstimate = estimate
                if estimate != nil && self?.connectionState == .syncing {
                    self?.connectionState = .buffering
                }
            }
        }
        clock.onStatus = { [weak self] status in
            DispatchQueue.main.async { [weak self] in
                self?.clockStatus = status
            }
        }
        discovery.onHosts = { [weak self] hosts in
            DispatchQueue.main.async { [weak self] in self?.discoveredHosts = hosts }
        }
        discovery.onStatus = { [weak self] status in
            DispatchQueue.main.async { [weak self] in self?.discoveryStatus = status }
        }
        control.onState = { [weak self] status in
            DispatchQueue.main.async { [weak self] in self?.controlStatus = status }
        }
        control.onLost = { [weak self] in
            DispatchQueue.main.async { [weak self] in
                guard let self, self.isListening else { return }
                self.receiver.stop()
                self.audio.stop()
                self.clock.stop()
                self.expectedSessionID = nil
                self.connectionState = .reconnecting
            }
        }
        control.onWelcome = { [weak self] welcome in
            DispatchQueue.main.async { [weak self] in
                guard let self, self.isListening else { return }
                self.hostIP = String(describing: welcome.hostIPv4)
                self.portText = String(welcome.audioPort)
                self.controlPortText = String(welcome.clockPort)
                self.startStream(host: welcome.hostIPv4,
                                 audioPort: welcome.audioPort,
                                 clockPort: welcome.clockPort,
                                 sessionID: welcome.sessionID,
                                 streamID: welcome.streamID)
            }
        }
        discovery.start()
    }

    func connect(to host: DiscoveredHost) {
        guard !isListening else { return }
        isListening = true
        connectionState = .connecting
        snapshot.status = "Joining \(host.name)"
        control.join(endpoint: host.endpoint)
    }

    func start() {
        let addressText = hostIP.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let address = IPv4Address(addressText) else {
            snapshot.status = "Enter the desktop host's IPv4 address"
            return
        }
        guard let port = UInt16(portText), port > 0 else {
            snapshot.status = "Enter a UDP port from 1 to 65535"
            return
        }
        guard let controlPort = UInt16(controlPortText), controlPort > 0,
              controlPort != port else {
            snapshot.status = "Enter a different clock UDP port from 1 to 65535"
            return
        }
        guard let sessionPort = UInt16(sessionPortText), sessionPort > 0,
              sessionPort != port, sessionPort != controlPort else {
            snapshot.status = "Enter a different session TCP port"
            return
        }
        isListening = true
        connectionState = .connecting
        snapshot.status = "Joining host"
        control.join(endpoint: .hostPort(host: .ipv4(address),
                                         port: NWEndpoint.Port(rawValue: sessionPort)!))
    }

    private func startStream(host address: IPv4Address,
                             audioPort port: UInt16, clockPort controlPort: UInt16,
                             sessionID: UInt64, streamID: UInt32) {
        isListening = true
        expectedSessionID = sessionID
        connectionState = .syncing
        audio.start()
        receiver.start(host: address, port: port,
                       sessionID: sessionID, streamID: streamID)
        clock.start(host: address, port: controlPort)
    }

    func stop() {
        isListening = false
        expectedSessionID = nil
        connectionState = .disconnected
        receiver.stop()
        audio.stop()
        clock.stop()
        control.stop()
    }

    func exportDiagnostics() {
        let values: [String: Any] = [
            "deviceId": control.deviceIdentifier,
            "sessionId": expectedSessionID.map { String($0) } ?? "unavailable",
            "connectionState": connectionState.rawValue,
            "sampleRate": snapshot.sampleRate.map(Int.init) ?? 0,
            "networkRTTMs": clockEstimate?.roundTripMilliseconds as Any? ?? NSNull(),
            "networkJitterMs": snapshot.networkJitterMilliseconds,
            "packetLossPercent": snapshot.packetLossPercent,
            "clockOffsetMs": clockEstimate?.offsetMilliseconds as Any? ?? NSNull(),
            "estimatedDriftPpm": clockEstimate?.estimatedDriftPpm as Any? ?? NSNull(),
            "targetBufferMs": playback.targetBufferMilliseconds,
            "actualBufferMs": playback.queuedMilliseconds,
            "latePackets": playback.latePackets,
            "underruns": playback.underruns,
            "estimatedOutputLatencyMs": playback.outputLatencyMilliseconds as Any? ?? NSNull(),
            "estimatedSyncErrorMs": playback.estimatedSyncErrorMilliseconds as Any? ?? NSNull(),
            "exportedAt": ISO8601DateFormatter().string(from: Date())
        ]
        do {
            let data = try JSONSerialization.data(withJSONObject: values,
                                                  options: [.prettyPrinted, .sortedKeys])
            let url = FileManager.default.temporaryDirectory
                .appendingPathComponent("tandem-diagnostics-\(UUID().uuidString).json")
            try data.write(to: url, options: .atomic)
            diagnosticsURL = url
        } catch {
            snapshot.status = "Could not export diagnostics: \(error.localizedDescription)"
        }
    }
}
