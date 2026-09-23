import Foundation

/// Keeps packet order independent of UDP arrival order. All methods run on the audio queue.
struct PacketPlaybackQueue {
    enum Item {
        case packet(AudioPacket)
        case silence(Int)

        var frames: Int {
            switch self {
            case .packet(let packet): return Int(packet.frameCount)
            case .silence(let count): return count
            }
        }
    }

    private(set) var cursor: UInt64?
    private(set) var packets = [UInt64: AudioPacket]()
    private(set) var sampleRate: UInt32?
    private(set) var channels: UInt16?
    private(set) var sessionID: UInt64?

    var bufferedFrames: UInt64 {
        guard let first = cursor,
              let last = packets.values.map({ $0.startFrame + UInt64($0.frameCount) }).max() else {
            return 0
        }
        return last > first ? last - first : 0
    }

    var firstPacket: AudioPacket? {
        guard let first = packets.keys.min() else { return nil }
        return packets[first]
    }

    mutating func discard(beforeHostNanoseconds timestamp: Double) {
        packets = packets.filter { Double($0.value.presentationTimestampNanoseconds) >= timestamp }
        cursor = packets.keys.min()
    }

    mutating func insert(_ packet: AudioPacket) {
        if sessionID != packet.sessionID || sampleRate != packet.sampleRate || channels != packet.channels {
            self = PacketPlaybackQueue()
            sessionID = packet.sessionID
            sampleRate = packet.sampleRate
            channels = packet.channels
        }
        if let cursor, packet.startFrame < cursor { return }
        packets[packet.startFrame] = packet
        if cursor == nil { cursor = packet.startFrame }
        // Limit memory if audio output is paused or cannot start.
        if packets.count > 512, let oldest = packets.keys.min() {
            packets.removeValue(forKey: oldest)
            cursor = packets.keys.min()
        }
    }

    mutating func popNext() -> Item? {
        guard let cursor, !packets.isEmpty else { return nil }
        if let packet = packets.removeValue(forKey: cursor) {
            self.cursor = cursor + UInt64(packet.frameCount)
            return .packet(packet)
        }
        guard let next = packets.keys.min(), next > cursor else { return nil }
        let count = Int(min(next - cursor, 1_024))
        self.cursor = cursor + UInt64(count)
        return .silence(count)
    }
}
