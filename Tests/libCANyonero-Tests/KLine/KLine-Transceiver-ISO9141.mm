///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <vector>

#import "KLine.hpp"

using namespace CANyonero::KLine;
using CANyonero::Bytes;

static uint8_t calcChecksum(const std::vector<uint8_t>& frame) {
    uint8_t sum = 0;
    for (size_t i = 0; i + 1 < frame.size(); ++i) { sum = static_cast<uint8_t>(sum + frame[i]); }
    return sum;
}

@interface KLine_Transceiver_ISO9141 : XCTestCase
@end

@implementation KLine_Transceiver_ISO9141

-(void)testISO9141SingleFrameFinalize {
    // Example ISO 9141 response: 48 6B 11 41 0D 00 12 (checksum is simple sum)
    auto frame = std::vector<uint8_t>{ 0x48, 0x6B, 0x11, 0x41, 0x0D, 0x00, 0x12 };

    Transceiver iso(0x48, 0x6B, 0, ProtocolMode::iso9141);
    auto action = iso.feed(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::waitForMore);

    auto finalAction = iso.finalize();
    XCTAssertEqual(finalAction.type, Transceiver::Action::Type::process);
    auto expected = std::vector<uint8_t>{ 0x41, 0x0D, 0x00 };
    XCTAssertEqual(finalAction.data, expected);
}

-(void)testISO9141ExpectedLengthEmitsProcess {
    auto frame = std::vector<uint8_t>{ 0x48, 0x6B, 0x11, 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11, 0xCA };

    Transceiver iso(0x48, 0x6B, 6, ProtocolMode::iso9141); // expect data length 6 bytes
    auto action = iso.feed(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::process);

    auto expected = std::vector<uint8_t>{ 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11 };
    XCTAssertEqual(action.data, expected);
}

-(void)testISO9141AddressMismatch {
    auto frame = std::vector<uint8_t>{ 0x48, 0x6B, 0x11, 0x41, 0x0D, 0x00, 0x12 };

    Transceiver iso(0x11, 0x6B, 0, ProtocolMode::iso9141); // mismatch target
    auto action = iso.feed(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testISOFrameInKwpModeIsRejected {
    // When parsed as KWP, the format byte/length do not match actual size.
    auto frame = std::vector<uint8_t>{ 0x48, 0x6B, 0x11, 0x41, 0x0D, 0x00, 0x12 };

    Transceiver kwp(0x6B, 0x11);
    auto action = kwp.feed(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testISO9141StripsRepeatedHeadersAndSequence {
    // ISO 9141 responses sometimes repeat service/PID and sequence indices per frame.
    // Expect the transceiver to merge and strip those repetitions.
    auto f1 = std::vector<uint8_t>{ 0x48, 0x6B, 0x11, 0x49, 0x02, 0x01, 0xAA, 0x00 };
    f1.back() = calcChecksum(f1);
    auto f2 = std::vector<uint8_t>{ 0x48, 0x6B, 0x11, 0x49, 0x02, 0x02, 0xBB, 0x00 };
    f2.back() = calcChecksum(f2);

    Transceiver iso(0x48, 0x6B, 0, ProtocolMode::iso9141);
    XCTAssertEqual(iso.feed(f1).type, Transceiver::Action::Type::waitForMore);
    XCTAssertEqual(iso.feed(f2).type, Transceiver::Action::Type::waitForMore);

    auto finalAction = iso.finalize();
    XCTAssertEqual(finalAction.type, Transceiver::Action::Type::process);
    auto expected = std::vector<uint8_t>{ 0x49, 0x02, 0xAA, 0xBB };
    XCTAssertEqual(finalAction.data, expected);
}

@end
