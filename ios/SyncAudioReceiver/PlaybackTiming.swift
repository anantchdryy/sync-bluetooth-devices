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

struct OutputLatencyModel {
    let outputRouteId: String
    let outputRouteType: String
    let systemEstimateMs: Double
    let manualAdjustmentMs: Double
    let calibrationConfidence: String

    var effectiveLatencyMs: Double {
        systemEstimateMs + min(1_000, max(-1_000, manualAdjustmentMs))
    }

    var effectiveLatencyNs: Double { effectiveLatencyMs * 1_000_000 }
}
