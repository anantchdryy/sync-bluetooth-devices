import Foundation
import Network

struct ClockEstimate {
    /// Host monotonic time minus iPhone monotonic time.
    let offsetNanoseconds: Double
    let roundTripNanoseconds: Double
    let measuredAtNanoseconds: UInt64
    let sampleCount: Int

    var offsetMilliseconds: Double { offsetNanoseconds / 1_000_000 }
    var roundTripMilliseconds: Double { roundTripNanoseconds / 1_000_000 }

    func localTime(forHostNanoseconds hostTime: UInt64) -> Double {
        Double(hostTime) - offsetNanoseconds
    }
}

enum ClockSyncWire {
    static let messageSize = 40

    static func request(id: UInt32, sentAt: UInt64) -> Data {
        var bytes = [UInt8](repeating: 0, count: messageSize)
        bytes.replaceSubrange(0...3, with: [0x53, 0x43, 0x4c, 0x4b])
        bytes[4] = 1
        bytes[5] = 1
        bytes[7] = UInt8(messageSize)
        put(id, in: &bytes, at: 8)
        put(sentAt, in: &bytes, at: 16)
        return Data(bytes)
    }

    static func response(_ data: Data, id: UInt32, sentAt: UInt64,
                         receivedAt: UInt64) -> ClockEstimate? {
        let bytes = [UInt8](data)
        guard bytes.count == messageSize,
              bytes[0...3].elementsEqual([0x53, 0x43, 0x4c, 0x4b]),
              bytes[4] == 1, bytes[5] == 2, bytes[6] == 0,
              bytes[7] == UInt8(messageSize),
              read32(bytes, at: 8) == id,
              read64(bytes, at: 16) == sentAt,
              receivedAt >= sentAt else { return nil }
        let hostReceive = read64(bytes, at: 24)
        let hostSend = read64(bytes, at: 32)
        guard hostSend >= hostReceive else { return nil }
        let roundTrip = Double(receivedAt - sentAt) - Double(hostSend - hostReceive)
        guard roundTrip >= 0, roundTrip < 1_000_000_000 else { return nil }
        let offset = ((Double(hostReceive) - Double(sentAt)) +
                      (Double(hostSend) - Double(receivedAt))) / 2
        return ClockEstimate(offsetNanoseconds: offset,
                             roundTripNanoseconds: roundTrip,
                             measuredAtNanoseconds: receivedAt, sampleCount: 1)
    }

    private static func put(_ value: UInt32, in bytes: inout [UInt8], at offset: Int) {
        for i in 0..<4 { bytes[offset + i] = UInt8(truncatingIfNeeded: value >> (24 - i * 8)) }
    }

    private static func put(_ value: UInt64, in bytes: inout [UInt8], at offset: Int) {
        for i in 0..<8 { bytes[offset + i] = UInt8(truncatingIfNeeded: value >> (56 - i * 8)) }
    }

    private static func read32(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        (0..<4).reduce(0) { ($0 << 8) | UInt32(bytes[offset + $1]) }
    }

    private static func read64(_ bytes: [UInt8], at offset: Int) -> UInt64 {
        (0..<8).reduce(0) { ($0 << 8) | UInt64(bytes[offset + $1]) }
    }
}

/// Periodic SCLK UDP probes. The lowest-RTT recent sample limits queueing bias.
final class HostClockSync {
    var onEstimate: ((ClockEstimate?) -> Void)?
    var onStatus: ((String) -> Void)?

    private let queue = DispatchQueue(label: "SyncAudioReceiver.clock")
    private var connection: NWConnection?
    private var timer: DispatchSourceTimer?
    private var generation = 0
    private var nextID: UInt32 = 1
    private var pending: (id: UInt32, sentAt: UInt64)?
    private var samples = [ClockEstimate]()
    private var lastProbe: UInt64 = 0

    func start(host: IPv4Address, port: UInt16) {
        queue.async { [weak self] in
            guard let self, let endpointPort = NWEndpoint.Port(rawValue: port) else { return }
            self.generation += 1
            self.cancelCurrent()
            let currentGeneration = self.generation
            let connection = NWConnection(host: .ipv4(host), port: endpointPort, using: .udp)
            self.connection = connection
            connection.stateUpdateHandler = { [weak self] state in
                guard let self, self.generation == currentGeneration else { return }
                switch state {
                case .ready:
                    self.onStatus?("Probing host clock")
                    self.receiveNext(generation: currentGeneration)
                    self.probe()
                case .failed(let error):
                    self.onStatus?("Clock sync failed: \(error)")
                default: break
                }
            }
            connection.start(queue: self.queue)
            let timer = DispatchSource.makeTimerSource(queue: self.queue)
            timer.schedule(deadline: .now() + .milliseconds(500), repeating: .milliseconds(500))
            timer.setEventHandler { [weak self] in self?.probe() }
            self.timer = timer
            timer.resume()
        }
    }

    func stop() {
        queue.async { [weak self] in
            guard let self else { return }
            self.generation += 1
            self.cancelCurrent()
            self.onEstimate?(nil)
            self.onStatus?("Idle")
        }
    }

    private func probe() {
        guard let connection else { return }
        let now = DispatchTime.now().uptimeNanoseconds
        if let pending, now - pending.sentAt < 400_000_000 { return }
        if samples.count >= 8 && now - lastProbe < 5_000_000_000 { return }
        let id = nextID
        nextID &+= 1
        lastProbe = now
        pending = (id, now)
        connection.send(content: ClockSyncWire.request(id: id, sentAt: now),
                        completion: .contentProcessed { [weak self] error in
            if let error { self?.onStatus?("Clock probe send failed: \(error)") }
        })
    }

    private func receiveNext(generation: Int) {
        connection?.receiveMessage { [weak self] data, _, _, error in
            guard let self, self.generation == generation else { return }
            if let error { self.onStatus?("Clock response failed: \(error)") }
            if let data, let pending,
               let sample = ClockSyncWire.response(data, id: pending.id,
                                                   sentAt: pending.sentAt,
                                                   receivedAt: DispatchTime.now().uptimeNanoseconds) {
                self.pending = nil
                self.samples.append(sample)
                if self.samples.count > 8 { self.samples.removeFirst() }
                if let best = self.samples.min(by: { $0.roundTripNanoseconds < $1.roundTripNanoseconds }) {
                    let estimate = ClockEstimate(offsetNanoseconds: best.offsetNanoseconds,
                                                 roundTripNanoseconds: best.roundTripNanoseconds,
                                                 measuredAtNanoseconds: best.measuredAtNanoseconds,
                                                 sampleCount: self.samples.count)
                    self.onEstimate?(estimate)
                    self.onStatus?("Clock synced")
                }
            }
            self.receiveNext(generation: generation)
        }
    }

    private func cancelCurrent() {
        timer?.cancel()
        timer = nil
        connection?.stateUpdateHandler = nil
        connection?.cancel()
        connection = nil
        pending = nil
        samples.removeAll()
        lastProbe = 0
    }
}
