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

struct ScheduledHostAction: Equatable {
    let name: String
    let effectiveHostNanoseconds: UInt64
    let seekFrame: UInt64
    let nextStreamID: UInt32
}

/// A reliable, bounded, line-oriented session channel. PCM stays on UDP.
final class HostControlChannel {
    var onWelcome: ((HostWelcome) -> Void)?
    var onState: ((String) -> Void)?
    var onLost: (() -> Void)?
    var onRoom: ((String, String) -> Void)?
    var onSyncAt: ((UInt64) -> Void)?
    var onHostPlayback: ((String, UInt64) -> Void)?
    var onScheduledAction: ((ScheduledHostAction) -> Void)?

    private let queue = DispatchQueue(label: "TandemAudio.control")
    private var connection: NWConnection?
    private var timer: DispatchSourceTimer?
    private var retryTimer: DispatchSourceTimer?
    private var generation = 0
    private var desiredEndpoint: NWEndpoint?
    private var retryAttempt = 0
    private var awaitingWelcomeSeconds = 0
    private var joinedSessionID: UInt64?
    private var latestClientState: String?
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
            awaitingWelcomeSeconds = 0
            joinedSessionID = nil
            let currentGeneration = generation
            let connection = NWConnection(to: endpoint, using: .tcp)
            self.connection = connection
            connection.stateUpdateHandler = { [weak self] state in
                guard let self, self.generation == currentGeneration else { return }
                switch state {
                case .ready:
                    self.onState?("Connected to host")
                    self.send("HELLO 2")
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

    func updateClientState(_ state: String, rtt: Double, jitter: Double,
                           loss: Double, buffer: Double, syncError: Double,
                           outputLatency: Double, offset: Double) {
        queue.async { [weak self] in
            let values = [rtt, jitter, loss, buffer, syncError,
                          outputLatency, offset]
            guard let self, values.allSatisfy(\.isFinite) else { return }
            self.latestClientState = "CLIENT_STATE \(state) " +
                values.map { String($0) }.joined(separator: " ")
        }
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
        if let welcome = HostWelcome(line: line) {
            retryAttempt = 0
            joinedSessionID = welcome.sessionID
            onWelcome?(welcome)
        } else if line.hasPrefix("HOST_STATE ") {
            let fields = line.split(separator: " ")
            if fields.count >= 4, let session = UInt64(fields[2]),
               session != joinedSessionID {
                scheduleRetry(reason: "Host session changed")
            } else {
                onState?(line)
                if fields.count >= 4, let frame = UInt64(fields[3]) {
                    onHostPlayback?(String(fields[1]), frame)
                }
                if let index = fields.firstIndex(where: { $0 == "PENDING" || $0 == "ACTION" }),
                   fields.count >= index + 5,
                   let timestamp = UInt64(fields[index + 2]),
                   let frame = UInt64(fields[index + 3]),
                   let streamID = UInt32(fields[index + 4]) {
                    onScheduledAction?(ScheduledHostAction(
                        name: String(fields[index + 1]),
                        effectiveHostNanoseconds: timestamp,
                        seekFrame: frame, nextStreamID: streamID))
                }
            }
        } else if line.hasPrefix("ROOM ") {
            let fields = line.split(separator: " ", maxSplits: 2)
            if fields.count == 3 { onRoom?(String(fields[1]), String(fields[2])) }
        } else if line.hasPrefix("SYNC_AT ") {
            if let value = UInt64(line.dropFirst(8)) { onSyncAt?(value) }
        } else if line == "VERSION 2" {
            onState?("Protocol version 2")
        } else if line.hasPrefix("ERROR ") {
            scheduleRetry(reason: "Host rejected session")
        }
    }

    private func startTimer(generation: Int) {
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + .seconds(1), repeating: .seconds(1))
        timer.setEventHandler { [weak self] in
            guard let self, self.generation == generation else { return }
            if self.joinedSessionID == nil {
                self.awaitingWelcomeSeconds += 1
                if self.awaitingWelcomeSeconds >= 3 {
                    self.scheduleRetry(reason: "Host did not complete JOIN")
                    return
                }
            }
            self.send("HOST_STATE")
            if self.joinedSessionID != nil, let state = self.latestClientState {
                self.send(state)
            }
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
