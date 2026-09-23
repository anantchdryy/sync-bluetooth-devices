import Combine
import Foundation
import Network

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

    private let receiver = UDPStreamReceiver()
    private let audio = AudioPlaybackController()
    private let clock = HostClockSync()
    private let discovery = HostDiscovery()
    private let control = HostControlChannel()
    private var awaitingDiscoveredWelcome = false

    init() {
        receiver.onSnapshot = { [weak self] snapshot in
            self?.audio.updateTargetBuffer(milliseconds: snapshot.targetBufferMilliseconds)
            DispatchQueue.main.async { [weak self] in
                self?.snapshot = snapshot
            }
        }
        receiver.onPacket = { [weak self] packet in
            self?.audio.receive(packet)
        }
        audio.onSnapshot = { [weak self] playback in
            DispatchQueue.main.async { [weak self] in
                self?.playback = playback
            }
        }
        clock.onEstimate = { [weak self] estimate in
            self?.audio.updateClock(estimate)
            DispatchQueue.main.async { [weak self] in
                self?.clockEstimate = estimate
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
        control.onWelcome = { [weak self] welcome in
            DispatchQueue.main.async { [weak self] in
                guard let self, self.awaitingDiscoveredWelcome else { return }
                self.awaitingDiscoveredWelcome = false
                self.hostIP = String(describing: welcome.hostIPv4)
                self.portText = String(welcome.audioPort)
                self.controlPortText = String(welcome.clockPort)
                self.startStream(host: welcome.hostIPv4,
                                 audioPort: welcome.audioPort,
                                 clockPort: welcome.clockPort)
            }
        }
        discovery.start()
    }

    func connect(to host: DiscoveredHost) {
        guard !isListening else { return }
        awaitingDiscoveredWelcome = true
        isListening = true
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
        startStream(host: address, audioPort: port, clockPort: controlPort)
        control.join(endpoint: .hostPort(host: .ipv4(address),
                                         port: NWEndpoint.Port(rawValue: sessionPort)!))
    }

    private func startStream(host address: IPv4Address,
                             audioPort port: UInt16, clockPort controlPort: UInt16) {
        isListening = true
        audio.start()
        receiver.start(host: address, port: port)
        clock.start(host: address, port: controlPort)
    }

    func stop() {
        isListening = false
        awaitingDiscoveredWelcome = false
        receiver.stop()
        audio.stop()
        clock.stop()
        control.stop()
    }
}
