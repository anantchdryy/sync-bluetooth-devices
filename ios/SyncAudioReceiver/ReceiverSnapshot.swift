import AVFoundation
import Foundation

struct ReceiverSnapshot {
    var status = "Idle"
    var lastSequenceNumber: UInt32?
    var packetsPerSecond = 0
    var packetsReceived: UInt64 = 0
    var packetsLost: UInt64 = 0
    var sampleRate: UInt32?
    var channels: UInt16?
    var bufferDepthMilliseconds = 0.0
    var audioFormatSupported = false
}

/// Packet statistics and a bounded history for receive-depth diagnostics.
struct StreamAccumulator {
    private(set) var snapshot = ReceiverSnapshot()
    private var sessionID: UInt64?
    private var highestSequence: UInt32?
    private var missingSequences = Set<UInt32>()
    private var recentSequences = Set<UInt32>()
    private var recentSequenceOrder = [UInt32]()
    private var arrivals = [TimeInterval]()
    private var packets = [AudioPacket]()
    private var bufferedFrames: UInt64 = 0
    private var bufferedBytes = 0
    private var lastArrival: TimeInterval?

    mutating func setStatus(_ status: String) {
        snapshot.status = status
    }

    @discardableResult
    mutating func record(_ packet: AudioPacket, at now: TimeInterval) -> Bool {
        if sessionID != packet.sessionID {
            self = StreamAccumulator()
            sessionID = packet.sessionID
        }
        if let sampleRate = snapshot.sampleRate,
           (sampleRate != packet.sampleRate || snapshot.channels != packet.channels) {
            return false
        }
        if !recentSequences.insert(packet.sequenceNumber).inserted {
            return false
        }
        recentSequenceOrder.append(packet.sequenceNumber)
        if recentSequenceOrder.count > 4_096 {
            recentSequences.remove(recentSequenceOrder.removeFirst())
        }

        if let highestSequence {
            let delta = Int32(bitPattern: packet.sequenceNumber &- highestSequence)
            if delta == 0 {
                return false
            }
            if delta > 0 {
                let gap = UInt32(delta - 1)
                snapshot.packetsLost += UInt64(gap)
                if gap <= 4_096 && missingSequences.count + Int(gap) <= 4_096 {
                    var missing = highestSequence &+ 1
                    for _ in 0..<gap {
                        missingSequences.insert(missing)
                        missing = missing &+ 1
                    }
                }
                self.highestSequence = packet.sequenceNumber
            } else if missingSequences.remove(packet.sequenceNumber) != nil {
                snapshot.packetsLost -= 1
            }
        } else {
            highestSequence = packet.sequenceNumber
        }

        snapshot.status = "Receiving"
        snapshot.lastSequenceNumber = packet.sequenceNumber
        snapshot.packetsReceived += 1
        snapshot.sampleRate = packet.sampleRate
        snapshot.channels = packet.channels
        if snapshot.packetsReceived == 1 {
            snapshot.audioFormatSupported = AVAudioFormat(
                commonFormat: .pcmFormatInt16,
                sampleRate: Double(packet.sampleRate),
                channels: AVAudioChannelCount(packet.channels),
                interleaved: true
            ) != nil
        }

        lastArrival = now
        arrivals.append(now)
        packets.append(packet)
        bufferedFrames += UInt64(packet.frameCount)
        bufferedBytes += packet.pcmPayload.count
        trim(at: now)
        return true
    }

    mutating func refresh(at now: TimeInterval) {
        trim(at: now)
        if let lastArrival, now - lastArrival > 2,
           snapshot.status == "Receiving" {
            snapshot.status = "Waiting for host packets"
        }
    }

    private mutating func trim(at now: TimeInterval) {
        arrivals.removeAll { now - $0 >= 1 }
        snapshot.packetsPerSecond = arrivals.count
        if let sampleRate = snapshot.sampleRate {
            let maximumFrames = UInt64(sampleRate) / 2
            while !packets.isEmpty &&
                    (bufferedFrames > maximumFrames || bufferedBytes > 512 * 1_024) {
                let removed = packets.removeFirst()
                bufferedFrames -= UInt64(removed.frameCount)
                bufferedBytes -= removed.pcmPayload.count
            }
            snapshot.bufferDepthMilliseconds =
                Double(bufferedFrames) * 1_000 / Double(sampleRate)
        }
    }
}
