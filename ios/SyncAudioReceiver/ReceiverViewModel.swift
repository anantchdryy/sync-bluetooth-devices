import Combine
import Foundation
import Network

@MainActor
final class ReceiverViewModel: ObservableObject {
    @Published var hostIP = ""
    @Published var portText = "40100"
    @Published private(set) var snapshot = ReceiverSnapshot()
    @Published private(set) var isListening = false

    private let receiver = UDPStreamReceiver()

    init() {
        receiver.onSnapshot = { [weak self] snapshot in
            DispatchQueue.main.async { [weak self] in
                self?.snapshot = snapshot
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
        isListening = true
        receiver.start(host: address, port: port)
    }

    func stop() {
        isListening = false
        receiver.stop()
    }
}
