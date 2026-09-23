import Foundation
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

    private func makeDatagram(sequence: UInt32) -> Data {
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
        bytes[45] = 4 // payload bytes
        bytes[47] = 2 // frames
        bytes[48] = 1
        bytes[49] = 0
        bytes[50] = 2
        bytes[51] = 0
        return Data(bytes)
    }
}
