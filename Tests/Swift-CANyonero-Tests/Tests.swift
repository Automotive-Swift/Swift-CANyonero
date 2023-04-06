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

    func testParseInfo() throws {

        let vendor = std.__1.string("Vanille-Media")
        let model = std.__1.string("CANyonero Basic")
        let hardware = std.__1.string("ESP32/A0")
        let serial = std.__1.string("1234567890")
        let firmware = std.__1.string("0.0.1")

        let info = CANyonero.PDU.info(vendor, model, hardware, serial, firmware)
        let pdu = CANyonero.PDU.init(info.frame());
        XCTAssertEqual(pdu.type(), CANyonero.PDUType.info);
        let parsedInfo = pdu.information()
        XCTAssertEqual(parsedInfo.vendor, vendor)
        XCTAssertEqual(parsedInfo.model, model)
        XCTAssertEqual(parsedInfo.hardware, hardware)
        XCTAssertEqual(parsedInfo.serial, serial)
        XCTAssertEqual(parsedInfo.firmware, firmware)
    }

}
