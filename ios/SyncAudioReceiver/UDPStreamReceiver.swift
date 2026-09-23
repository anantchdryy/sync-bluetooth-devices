import Foundation
import Network

/// Receives version-2 SAUD UDP datagrams from one configured IPv4 host.
final class UDPStreamReceiver {
    var onSnapshot: ((ReceiverSnapshot) -> Void)?
    var onPacket: ((AudioPacket) -> Void)?

    private let queue = DispatchQueue(label: "SyncAudioReceiver.udp")
    private var listener: NWListener?
    private var connections = [ObjectIdentifier: NWConnection]()
    private var updateTimer: DispatchSourceTimer?
    private var expectedHost: IPv4Address?
    private var accumulator = StreamAccumulator()
    private var generation = 0

    func start(host: IPv4Address, port: UInt16) {
        queue.async { [weak self] in
            guard let self else { return }
            self.generation += 1
            let currentGeneration = self.generation
            self.cancelCurrent()
            self.expectedHost = host
            self.accumulator = StreamAccumulator()
            self.accumulator.setStatus("Starting listener")
            self.publish()

            guard let endpointPort = NWEndpoint.Port(rawValue: port) else {
                self.accumulator.setStatus("Invalid UDP port")
                self.publish()
                return
            }
            do {
                let listener = try NWListener(using: .udp, on: endpointPort)
                self.listener = listener
                listener.stateUpdateHandler = { [weak self] state in
                    guard let self, self.generation == currentGeneration else { return }
                    switch state {
                    case .ready:
                        self.accumulator.setStatus("Waiting for host packets")
                    case .waiting(let error):
                        self.accumulator.setStatus("Network waiting: \(error)")
                    case .failed(let error):
                        self.accumulator.setStatus("Listener failed: \(error)")
                    case .cancelled:
                        return
                    default:
                        return
                    }
                    self.publish()
                }
                listener.newConnectionHandler = { [weak self] connection in
                    guard let self, self.generation == currentGeneration else {
                        connection.cancel()
                        return
                    }
                    self.accept(connection, generation: currentGeneration)
                }
                listener.start(queue: self.queue)
                self.startUpdateTimer(generation: currentGeneration)
            } catch {
                self.accumulator.setStatus("Cannot listen: \(error)")
                self.publish()
            }
        }
    }

    func stop() {
        queue.async { [weak self] in
            guard let self else { return }
            self.generation += 1
            self.cancelCurrent()
            self.expectedHost = nil
            self.accumulator = StreamAccumulator()
            self.publish()
        }
    }

    private func accept(_ connection: NWConnection, generation: Int) {
        guard let expectedHost,
              case let .hostPort(remoteHost, _) = connection.endpoint,
              case let .ipv4(remoteAddress) = remoteHost,
              remoteAddress.rawValue == expectedHost.rawValue else {
            connection.cancel()
            return
        }

        let identifier = ObjectIdentifier(connection)
        connections[identifier] = connection
        connection.stateUpdateHandler = { [weak self, weak connection] state in
            guard let self, let connection,
                  self.generation == generation else { return }
            switch state {
            case .ready:
                self.receiveNext(on: connection, generation: generation)
            case .failed, .cancelled:
                self.connections.removeValue(forKey: identifier)
            default:
                break
            }
        }
        connection.start(queue: queue)
    }

    private func receiveNext(on connection: NWConnection, generation: Int) {
        connection.receiveMessage { [weak self, weak connection] data, _, _, error in
            guard let self, let connection,
                  self.generation == generation else { return }
            if error != nil {
                self.connections.removeValue(forKey: ObjectIdentifier(connection))
                connection.cancel()
                return
            }
            if let data, let packet = AudioPacket(datagram: data) {
                if self.accumulator.record(packet, at: ProcessInfo.processInfo.systemUptime) {
                    self.onPacket?(packet)
                }
                if self.accumulator.snapshot.packetsReceived == 1 {
                    self.publish()
                }
            }
            self.receiveNext(on: connection, generation: generation)
        }
    }

    private func startUpdateTimer(generation: Int) {
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + .milliseconds(250),
                       repeating: .milliseconds(250))
        timer.setEventHandler { [weak self] in
            guard let self, self.generation == generation else { return }
            self.accumulator.refresh(at: ProcessInfo.processInfo.systemUptime)
            self.publish()
        }
        updateTimer = timer
        timer.resume()
    }

    private func cancelCurrent() {
        updateTimer?.cancel()
        updateTimer = nil
        listener?.stateUpdateHandler = nil
        listener?.newConnectionHandler = nil
        listener?.cancel()
        listener = nil
        for connection in connections.values {
            connection.stateUpdateHandler = nil
            connection.cancel()
        }
        connections.removeAll()
    }

    private func publish() {
        onSnapshot?(accumulator.snapshot)
    }
}
