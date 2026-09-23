import Foundation
import AVFoundation
import XCTest
@testable import SyncAudioReceiver

final class AudioPacketTests: XCTestCase {
    func testVersionOneDatagram() {
        let packet = AudioPacket(datagram: makeDatagram(sequence: 7))
        XCTAssertNotNil(packet)
        XCTAssertEqual(packet?.sessionID, 42)
        XCTAssertEqual(packet?.sequenceNumber, 7)
        XCTAssertEqual(packet?.sampleRate, 48_000)
        XCTAssertEqual(packet?.channels, 1)
        XCTAssertEqual(packet?.frameCount, 2)
        XCTAssertEqual(packet?.pcmPayload.count, 4)
    }

    func testMalformedPayloadIsRejected() {
        var datagram = makeDatagram(sequence: 0)
        datagram.removeLast()
        XCTAssertNil(AudioPacket(datagram: datagram))
    }

    func testMissingPacketCanArriveOutOfOrder() {
        var accumulator = StreamAccumulator()
        accumulator.record(AudioPacket(datagram: makeDatagram(sequence: 0))!, at: 10)
        accumulator.record(AudioPacket(datagram: makeDatagram(sequence: 2))!, at: 10.1)
        XCTAssertEqual(accumulator.snapshot.packetsLost, 1)
        accumulator.record(AudioPacket(datagram: makeDatagram(sequence: 1))!, at: 10.2)
        XCTAssertEqual(accumulator.snapshot.packetsLost, 0)
        XCTAssertEqual(accumulator.snapshot.packetsPerSecond, 3)
        XCTAssertGreaterThan(accumulator.snapshot.bufferDepthMilliseconds, 0)
    }

    func testPlaybackQueueReordersAndConcealsGap() {
        var queue = PacketPlaybackQueue()
        queue.insert(AudioPacket(datagram: makeDatagram(sequence: 0, startFrame: 0))!)
        queue.insert(AudioPacket(datagram: makeDatagram(sequence: 2, startFrame: 4))!)
        queue.insert(AudioPacket(datagram: makeDatagram(sequence: 1, startFrame: 2))!)
        XCTAssertEqual(queue.bufferedFrames, 6)
        XCTAssertEqual(queue.popNext()?.frames, 2)
        XCTAssertEqual(queue.popNext()?.frames, 2)
        XCTAssertEqual(queue.popNext()?.frames, 2)
        XCTAssertNil(queue.popNext())

        queue.insert(AudioPacket(datagram: makeDatagram(sequence: 4, startFrame: 8))!)
        if case .silence(let frames)? = queue.popNext() {
            XCTAssertEqual(frames, 2)
        } else {
            XCTFail("Missing frames should be concealed")
        }
    }

    func testPCMConversionPreservesSignedSampleValues() {
        let packet = AudioPacket(datagram: makeDatagram(sequence: 0))!
        let format = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: 48_000,
                                   channels: 1, interleaved: false)!
        let buffer = PCMBufferConverter.convert(packet, format: format)!
        XCTAssertEqual(buffer.frameLength, 2)
        XCTAssertEqual(buffer.floatChannelData![0][0], 1.0 / 32_768, accuracy: 0.000001)
        XCTAssertEqual(buffer.floatChannelData![0][1], 2.0 / 32_768, accuracy: 0.000001)
    }

    func testClockSyncWireAndTiming() {
        let sent: UInt64 = 1_000_000_000
        let received: UInt64 = 1_011_000_000
        var reply = [UInt8](ClockSyncWire.request(id: 7, sentAt: sent))
        reply[5] = 2
        write(1_025_000_000, into: &reply, at: 24)
        write(1_026_000_000, into: &reply, at: 32)
        let estimate = ClockSyncWire.response(Data(reply), id: 7, sentAt: sent,
                                              receivedAt: received)!
        XCTAssertEqual(estimate.offsetMilliseconds, 20, accuracy: 0.0001)
        XCTAssertEqual(estimate.roundTripMilliseconds, 10, accuracy: 0.0001)
        XCTAssertNil(ClockSyncWire.response(Data(reply), id: 8, sentAt: sent,
                                            receivedAt: received))

        let packet = AudioPacket(datagram: makeDatagram(sequence: 0,
                                                        presentationTime: 1_500_000_000))!
        XCTAssertEqual(PlaybackTiming.startDelayNanoseconds(packet: packet,
                          estimate: estimate, outputLatencyNanoseconds: 10_000_000,
                          nowNanoseconds: sent), 490_000_000, accuracy: 1)
    }

    func testClockMathWithFasterAndSlowerHost() {
        for offset: Int64 in [-30_000_000, 30_000_000] {
            let sent: UInt64 = 2_000_000_000
            let received = sent + 12_000_000
            var reply = [UInt8](ClockSyncWire.request(id: 1, sentAt: sent))
            reply[5] = 2
            write(UInt64(Int64(sent + 5_000_000) + offset), into: &reply, at: 24)
            write(UInt64(Int64(sent + 7_000_000) + offset), into: &reply, at: 32)
            let estimate = ClockSyncWire.response(Data(reply), id: 1, sentAt: sent,
                                                  receivedAt: received)!
            XCTAssertEqual(estimate.offsetNanoseconds, Double(offset), accuracy: 1)
            XCTAssertEqual(estimate.roundTripMilliseconds, 10, accuracy: 0.0001)
        }
    }

    private func makeDatagram(sequence: UInt32, startFrame: UInt64 = 0,
                              presentationTime: UInt64 = 0) -> Data {
        var bytes = [UInt8](repeating: 0, count: 52)
        bytes[0] = 0x53
        bytes[1] = 0x41
        bytes[2] = 0x55
        bytes[3] = 0x44
        bytes[4] = 1
        bytes[5] = 48
        bytes[15] = 42 // session ID
        bytes[16] = UInt8((sequence >> 24) & 0xff)
        bytes[17] = UInt8((sequence >> 16) & 0xff)
        bytes[18] = UInt8((sequence >> 8) & 0xff)
        bytes[19] = UInt8(sequence & 0xff)
        bytes[22] = 0xbb
        bytes[23] = 0x80 // 48 kHz
        bytes[25] = 1 // mono
        bytes[26] = 1 // signed 16-bit little-endian PCM
        bytes[35] = UInt8(startFrame & 0xff)
        write(presentationTime, into: &bytes, at: 36)
        bytes[45] = 4 // payload bytes
        bytes[47] = 2 // frames
        bytes[48] = 1
        bytes[49] = 0
        bytes[50] = 2
        bytes[51] = 0
        return Data(bytes)
    }

    private func write(_ value: UInt64, into bytes: inout [UInt8], at offset: Int) {
        for index in 0..<8 {
            bytes[offset + index] = UInt8(truncatingIfNeeded: value >> (56 - index * 8))
        }
    }
}
