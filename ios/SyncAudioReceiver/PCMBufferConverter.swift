import AVFoundation
import Foundation

enum PCMBufferConverter {
    static func convert(_ packet: AudioPacket, format: AVAudioFormat) -> AVAudioPCMBuffer? {
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format,
                                            frameCapacity: AVAudioFrameCount(packet.frameCount)),
              let channels = buffer.floatChannelData,
              format.channelCount == AVAudioChannelCount(packet.channels) else { return nil }
        let bytes = [UInt8](packet.pcmPayload)
        for frame in 0..<Int(packet.frameCount) {
            for channel in 0..<Int(packet.channels) {
                let offset = (frame * Int(packet.channels) + channel) * 2
                let sample = Int16(bitPattern: UInt16(bytes[offset]) | (UInt16(bytes[offset + 1]) << 8))
                channels[channel][frame] = Float(sample) / 32_768
            }
        }
        buffer.frameLength = AVAudioFrameCount(packet.frameCount)
        return buffer
    }

    static func silence(frames: Int, format: AVAudioFormat) -> AVAudioPCMBuffer? {
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format,
                                            frameCapacity: AVAudioFrameCount(frames)),
              let channels = buffer.floatChannelData else { return nil }
        for channel in 0..<Int(format.channelCount) {
            channels[channel].initialize(repeating: 0, count: frames)
        }
        buffer.frameLength = AVAudioFrameCount(frames)
        return buffer
    }
}
