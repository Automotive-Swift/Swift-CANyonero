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

@interface KWP_Transceiver_Valid : XCTestCase
@end

@implementation KWP_Transceiver_Valid

-(void)testSingleFrame {
    // 0x83 payload length, target 0xF1, source 0x10, payload 62 F1 90
    auto frame = std::vector<uint8_t>{ 0x83, 0xF1, 0x10, 0x62, 0xF1, 0x90, 0x00 };
    frame.back() = checksum(frame);

    Transceiver kwp(0xF1, 0x10);
    auto action = kwp.feed(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::waitForMore);

    auto finalAction = kwp.finalize();
    XCTAssertEqual(finalAction.type, Transceiver::Action::Type::process);
    auto expected = std::vector<uint8_t>{ 0x62, 0xF1, 0x90 };
    XCTAssertEqual(finalAction.data, expected);
}

-(void)testMultiFrameVINMerge {
    // Example VIN sequence
    auto f1 = std::vector<uint8_t>{ 0x87, 0xF1, 0x10, 0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x57, 0x00 };
    f1.back() = checksum(f1);
    auto f2 = std::vector<uint8_t>{ 0x87, 0xF1, 0x10, 0x49, 0x02, 0x02, 0x44, 0x58, 0x2D, 0x53, 0x00 };
    f2.back() = checksum(f2);
    auto f3 = std::vector<uint8_t>{ 0x87, 0xF1, 0x10, 0x49, 0x02, 0x03, 0x49, 0x4D, 0x30, 0x30, 0x00 };
    f3.back() = checksum(f3);
    auto f4 = std::vector<uint8_t>{ 0x87, 0xF1, 0x10, 0x49, 0x02, 0x04, 0x31, 0x39, 0x32, 0x31, 0x00 };
    f4.back() = checksum(f4);
    auto f5 = std::vector<uint8_t>{ 0x87, 0xF1, 0x10, 0x49, 0x02, 0x05, 0x32, 0x33, 0x34, 0x35, 0x00 };
    f5.back() = checksum(f5);

    Transceiver kwp(0xF1, 0x10);
    XCTAssertEqual(kwp.feed(f1).type, Transceiver::Action::Type::waitForMore);
    XCTAssertEqual(kwp.feed(f2).type, Transceiver::Action::Type::waitForMore);
    XCTAssertEqual(kwp.feed(f3).type, Transceiver::Action::Type::waitForMore);
    XCTAssertEqual(kwp.feed(f4).type, Transceiver::Action::Type::waitForMore);
    auto action = kwp.feed(f5);
    XCTAssertEqual(action.type, Transceiver::Action::Type::waitForMore);

    auto finalAction = kwp.finalize();
    XCTAssertEqual(finalAction.type, Transceiver::Action::Type::process);
    auto expected = std::vector<uint8_t>{
        0x49, 0x02, 0x00, 0x00, 0x00, 0x57, 0x44, 0x58, 0x2D, 0x53,
        0x49, 0x4D, 0x30, 0x30, 0x31, 0x39, 0x32, 0x31, 0x32, 0x33, 0x34, 0x35
    };
    XCTAssertEqual(finalAction.data, expected);
}

-(void)testExpectedLengthEmitsProcess {
    auto f1 = std::vector<uint8_t>{ 0x84, 0xF1, 0x10, 0x62, 0x01, 0x02, 0xAA, 0x00 };
    f1.back() = checksum(f1);
    auto f2 = std::vector<uint8_t>{ 0x83, 0xF1, 0x10, 0x62, 0x01, 0x03, 0x00 };
    f2.back() = checksum(f2);

    Transceiver kwp(0xF1, 0x10, 5); // expect 5 bytes total
    XCTAssertEqual(kwp.feed(f1).type, Transceiver::Action::Type::waitForMore);
    auto action = kwp.feed(f2);
    XCTAssertEqual(action.type, Transceiver::Action::Type::process);
    auto expected = std::vector<uint8_t>{ 0x62, 0x01, 0x02, 0xAA, 0x03 };
    XCTAssertEqual(action.data, expected);
}

@end
