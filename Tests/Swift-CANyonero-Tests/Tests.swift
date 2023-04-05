import XCTest
@testable import Swift_CANyonero

import CornucopiaCore

final class Swift_CANyonero_Tests: XCTestCase {

    func testCreatePing() throws {

        let bytes: [UInt8] = [1, 2 , 3, 4]
        let ping = CANyonero.PDU.ping(payload: bytes)
        ping.dump()

        let frame = ping.frame()
        XCTAssertEqual(frame.count, 8)
        XCTAssertEqual(frame[0], 0x1F)
        XCTAssertEqual(frame[1], 0x00)
        XCTAssertEqual(frame[2], 0x00)
        XCTAssertEqual(frame[3], 0x04)
        XCTAssertEqual(frame[4], 0x01)
        XCTAssertEqual(frame[5], 0x02)
        XCTAssertEqual(frame[6], 0x03)
        XCTAssertEqual(frame[7], 0x04)
    }

    func testCreateInfo() throws {

        let info = CANyonero.PDU.info("Vanille-Media", "CANyonero Basic", "ESP32/A0", "1234567890", "0.0.1")
        info.dump()
        
        let frame = info.frame()
    }

}
