import Foundation

enum PlaybackTiming {
    static let safetyDelayNanoseconds = 20_000_000.0
    static let minimumStartLeadNanoseconds = 100_000_000.0

    /// AVAudioPlayerNode renders before the sound reaches the output device.
    static func startDelayNanoseconds(packet: AudioPacket, estimate: ClockEstimate,
                                      outputLatencyNanoseconds: Double,
                                      nowNanoseconds: UInt64) -> Double {
        estimate.localTime(forHostNanoseconds: packet.presentationTimestampNanoseconds)
            + safetyDelayNanoseconds - outputLatencyNanoseconds - Double(nowNanoseconds)
    }

    static func minimumPacketTimestamp(estimate: ClockEstimate,
                                       outputLatencyNanoseconds: Double,
                                       nowNanoseconds: UInt64) -> Double {
        Double(nowNanoseconds) + estimate.offsetNanoseconds
            + outputLatencyNanoseconds + minimumStartLeadNanoseconds
            - safetyDelayNanoseconds
    }
}
