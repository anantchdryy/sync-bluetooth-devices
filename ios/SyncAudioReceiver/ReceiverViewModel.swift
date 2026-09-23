import Combine
import Foundation
import Network

@MainActor
final class ReceiverViewModel: ObservableObject {
    @Published var hostIP = ""
    @Published var portText = "40100"
    @Published var controlPortText = "40101"
    @Published private(set) var snapshot = ReceiverSnapshot()
    @Published private(set) var playback = PlaybackSnapshot()
    @Published private(set) var clockEstimate: ClockEstimate?
    @Published private(set) var clockStatus = "Idle"
    @Published private(set) var isListening = false

    private let receiver = UDPStreamReceiver()
    private let audio = AudioPlaybackController()
    private let clock = HostClockSync()

    init() {
        receiver.onSnapshot = { [weak self] snapshot in
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
        isListening = true
        audio.start()
        receiver.start(host: address, port: port)
        clock.start(host: address, port: controlPort)
    }

    func stop() {
        isListening = false
        receiver.stop()
        audio.stop()
        clock.stop()
    }
}
