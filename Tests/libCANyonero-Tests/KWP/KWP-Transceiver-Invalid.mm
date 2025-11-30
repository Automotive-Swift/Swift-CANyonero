///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <vector>

#import "KWP.hpp"

using namespace CANyonero::KWP;
using CANyonero::Bytes;

static uint8_t checksum(const std::vector<uint8_t>& frame) {
    uint8_t sum = 0;
    for (size_t i = 0; i + 1 < frame.size(); ++i) { sum = static_cast<uint8_t>(sum + frame[i]); }
    return sum;
}

@interface KWP_Transceiver_Invalid : XCTestCase
@end

@implementation KWP_Transceiver_Invalid

-(void)testChecksumInvalid {
    auto frame = std::vector<uint8_t>{ 0x83, 0xF1, 0x10, 0x62, 0xF1, 0x90, 0x00 };
    frame.back() = checksum(frame) + 1; // corrupt

    Transceiver kwp(0xF1, 0x10);
    auto action = kwp.feed(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testSizeMismatch {
    // Format claims 5-byte payload, but frame is too short.
    auto frame = std::vector<uint8_t>{ 0x85, 0xF1, 0x10, 0x62, 0x01, 0x02, 0xAA, 0x00 };
    frame.pop_back(); // drop checksum to make size mismatch

    Transceiver kwp(0xF1, 0x10);
    auto action = kwp.feed(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testSequenceMismatch {
    auto f1 = std::vector<uint8_t>{ 0x84, 0xF1, 0x10, 0x62, 0x01, 0x02, 0xAA, 0x00 };
    f1.back() = checksum(f1);
    auto f2 = std::vector<uint8_t>{ 0x83, 0xF1, 0x10, 0x62, 0x01, 0x05, 0x00 }; // seq jumps to 0x05
    f2.back() = checksum(f2);

    Transceiver kwp(0xF1, 0x10);
    XCTAssertEqual(kwp.feed(f1).type, Transceiver::Action::Type::waitForMore);
    auto action = kwp.feed(f2);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testBaseMismatch {
    auto f1 = std::vector<uint8_t>{ 0x84, 0xF1, 0x10, 0x62, 0x01, 0x02, 0xAA, 0x00 };
    f1.back() = checksum(f1);
    auto f2 = std::vector<uint8_t>{ 0x83, 0xF1, 0x10, 0x6F, 0x01, 0x03, 0x00 }; // different service
    f2.back() = checksum(f2);

    Transceiver kwp(0xF1, 0x10);
    XCTAssertEqual(kwp.feed(f1).type, Transceiver::Action::Type::waitForMore);
    auto action = kwp.feed(f2);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testUnexpectedAddress {
    auto frame = std::vector<uint8_t>{ 0x83, 0x01, 0x02, 0x62, 0xF1, 0x90, 0x00 };
    frame.back() = checksum(frame);

    Transceiver kwp(0xF1, 0x10);
    auto action = kwp.feed(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

@end
