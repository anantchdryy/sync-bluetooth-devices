import Foundation
import Network

struct HostWelcome {
    let sessionID: UInt64
    let streamID: UInt32
    let audioPort: UInt16
    let clockPort: UInt16
    let sampleRate: UInt32
    let channels: UInt16
    let hostIPv4: IPv4Address

    init?(line: String) {
        let fields = line.split(separator: " ")
        guard fields.count == 8, fields[0] == "WELCOME",
              let sessionID = UInt64(fields[1]), let streamID = UInt32(fields[2]),
              let audioPort = UInt16(fields[3]), let clockPort = UInt16(fields[4]),
              let sampleRate = UInt32(fields[5]), let channels = UInt16(fields[6]),
              let hostIPv4 = IPv4Address(String(fields[7])),
              sessionID > 0, streamID > 0, audioPort > 0, clockPort > 0,
              (8_000...192_000).contains(sampleRate), (1...2).contains(channels) else {
            return nil
        }
        self.sessionID = sessionID
        self.streamID = streamID
        self.audioPort = audioPort
        self.clockPort = clockPort
        self.sampleRate = sampleRate
        self.channels = channels
        self.hostIPv4 = hostIPv4
    }
}

/// A reliable, bounded, line-oriented session channel. PCM stays on UDP.
final class HostControlChannel {
    var onWelcome: ((HostWelcome) -> Void)?
    var onState: ((String) -> Void)?
    var onLost: (() -> Void)?

    private let queue = DispatchQueue(label: "TandemAudio.control")
    private var connection: NWConnection?
    private var timer: DispatchSourceTimer?
    private var retryTimer: DispatchSourceTimer?
    private var generation = 0
    private var desiredEndpoint: NWEndpoint?
    private var retryAttempt = 0
    private var input = Data()
    private let deviceID: String = {
        if let saved = UserDefaults.standard.string(forKey: "TandemAudio.deviceID") {
            return saved
        }
        let created = UUID().uuidString
        UserDefaults.standard.set(created, forKey: "TandemAudio.deviceID")
        return created
    }()
    var deviceIdentifier: String { deviceID }

    func join(endpoint: NWEndpoint) {
        queue.async { [weak self] in
            guard let self else { return }
            self.generation += 1
            self.desiredEndpoint = endpoint
            self.retryAttempt = 0
            self.cancelCurrent(sendLeave: true)
            self.startConnection()
        }
    }

    private func startConnection() {
            guard let endpoint = desiredEndpoint else { return }
            retryTimer?.cancel()
            retryTimer = nil
            generation += 1
            let currentGeneration = generation
            let connection = NWConnection(to: endpoint, using: .tcp)
            self.connection = connection
            connection.stateUpdateHandler = { [weak self] state in
                guard let self, self.generation == currentGeneration else { return }
                switch state {
                case .ready:
                    self.retryAttempt = 0
                    self.onState?("Connected to host")
                    self.send("JOIN \(self.deviceID)")
                    self.receive(generation: currentGeneration)
                    self.startTimer(generation: currentGeneration)
                case .failed(let error):
                    self.scheduleRetry(reason: "Host connection failed: \(error)")
                case .waiting(let error):
                    self.scheduleRetry(reason: "Host unavailable: \(error)")
                case .cancelled:
                    break
                default: break
                }
            }
            connection.start(queue: self.queue)
    }

    func stop() {
        queue.async { [weak self] in
            guard let self else { return }
            self.generation += 1
            self.desiredEndpoint = nil
            self.cancelCurrent(sendLeave: true)
            self.onState?("Disconnected")
        }
    }

    private func send(_ line: String) {
        connection?.send(content: Data((line + "\n").utf8), completion: .contentProcessed { _ in })
    }

    private func receive(generation: Int) {
        connection?.receive(minimumIncompleteLength: 1, maximumLength: 512) {
            [weak self] data, _, isComplete, error in
            guard let self, self.generation == generation else { return }
            if let data { self.input.append(data) }
            if self.input.count > 1_024 {
                self.scheduleRetry(reason: "Malformed control response")
                return
            }
            while let newline = self.input.firstIndex(of: 10) {
                let lineData = self.input.prefix(upTo: newline)
                self.input.removeSubrange(...newline)
                if let line = String(data: lineData, encoding: .utf8) {
                    self.handle(line)
                }
            }
            if isComplete || error != nil {
                self.scheduleRetry(reason: "Host connection lost")
                return
            }
            self.receive(generation: generation)
        }
    }

    private func handle(_ line: String) {
        if let welcome = HostWelcome(line: line) { onWelcome?(welcome) }
        else if line.hasPrefix("HOST_STATE ") { onState?(line) }
        else if line.hasPrefix("ERROR ") { onState?(line) }
    }

    private func startTimer(generation: Int) {
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + .seconds(1), repeating: .seconds(1))
        timer.setEventHandler { [weak self] in
            guard let self, self.generation == generation else { return }
            self.send("HOST_STATE")
        }
        self.timer = timer
        timer.resume()
    }

    private func cancelCurrent(sendLeave: Bool) {
        retryTimer?.cancel()
        retryTimer = nil
        timer?.cancel()
        timer = nil
        if sendLeave { send("LEAVE") }
        connection?.stateUpdateHandler = nil
        connection?.cancel()
        connection = nil
        input.removeAll()
    }

    private func scheduleRetry(reason: String) {
        guard desiredEndpoint != nil, retryTimer == nil else { return }
        onLost?()
        retryAttempt = min(retryAttempt + 1, 5)
        let delay = min(8, 1 << (retryAttempt - 1))
        onState?("Reconnecting in \(delay)s: \(reason)")
        generation += 1
        timer?.cancel()
        timer = nil
        connection?.stateUpdateHandler = nil
        connection?.cancel()
        connection = nil
        input.removeAll()
        let retry = DispatchSource.makeTimerSource(queue: queue)
        retry.schedule(deadline: .now() + .seconds(delay))
        retry.setEventHandler { [weak self] in self?.startConnection() }
        retryTimer = retry
        retry.resume()
    }
}
