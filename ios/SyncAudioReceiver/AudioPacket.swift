import Foundation

/// The desktop host's version-2 SAUD datagram. Multi-byte header fields are big-endian.
struct AudioPacket {
    static let headerSize = 52
    static let maximumDatagramSize = 1_200

    let sessionID: UInt64
    let streamID: UInt32
    let sequenceNumber: UInt32
    let sampleRate: UInt32
    let channels: UInt16
    let startFrame: UInt64
    let presentationTimestampNanoseconds: UInt64
    let frameCount: UInt16
    let pcmPayload: Data

    init?(datagram: Data) {
        let bytes = [UInt8](datagram)
        guard bytes.count >= Self.headerSize,
              bytes.count <= Self.maximumDatagramSize,
              bytes[0...3].elementsEqual([0x53, 0x41, 0x55, 0x44]),
              bytes[4] == 2,
              bytes[5] == Self.headerSize,
              bytes[26] == 1, bytes[27] == 0 else {
            return nil
        }

        let sessionID = Self.readUInt64(bytes, at: 8)
        let sequenceNumber = Self.readUInt32(bytes, at: 16)
        let sampleRate = Self.readUInt32(bytes, at: 20)
        let channels = Self.readUInt16(bytes, at: 24)
        let startFrame = Self.readUInt64(bytes, at: 28)
        let presentationTime = Self.readUInt64(bytes, at: 36)
        let payloadSize = Int(Self.readUInt16(bytes, at: 44))
        let frameCount = Self.readUInt16(bytes, at: 46)
        let streamID = Self.readUInt32(bytes, at: 48)

        guard (8_000...192_000).contains(sampleRate), channels > 0,
              channels <= 2, frameCount > 0, streamID > 0,
              startFrame <= UInt64.max - UInt64(frameCount),
              presentationTime <= UInt64(Int64.max),
              payloadSize <= Self.maximumDatagramSize - Self.headerSize,
              bytes.count == Self.headerSize + payloadSize,
              payloadSize == Int(frameCount) * Int(channels) * 2 else {
            return nil
        }

        self.sessionID = sessionID
        self.streamID = streamID
        self.sequenceNumber = sequenceNumber
        self.sampleRate = sampleRate
        self.channels = channels
        self.startFrame = startFrame
        self.presentationTimestampNanoseconds = presentationTime
        self.frameCount = frameCount
        self.pcmPayload = Data(bytes[Self.headerSize...])
    }

    private static func readUInt16(_ bytes: [UInt8], at offset: Int) -> UInt16 {
        (UInt16(bytes[offset]) << 8) | UInt16(bytes[offset + 1])
    }

    private static func readUInt32(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        (UInt32(readUInt16(bytes, at: offset)) << 16) |
            UInt32(readUInt16(bytes, at: offset + 2))
    }

    private static func readUInt64(_ bytes: [UInt8], at offset: Int) -> UInt64 {
        (UInt64(readUInt32(bytes, at: offset)) << 32) |
            UInt64(readUInt32(bytes, at: offset + 4))
    }
}
