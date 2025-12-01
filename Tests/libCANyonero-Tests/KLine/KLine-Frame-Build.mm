///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <vector>

#import "KLine.hpp"

using namespace CANyonero::KLine;
using CANyonero::Bytes;

@interface KLine_Frame_Build : XCTestCase
@end

@implementation KLine_Frame_Build

-(void)testBuildKwpFrameProducesValidChecksumAndLength {
    Bytes payload{ 0x62, 0xF1, 0x90 };
    auto frame = makeKwpFrame(0xF1, 0x10, payload);
    XCTAssertEqual(frame.size(), 3 + payload.size() + 1);
    // Expected format byte: 0x80 | payloadLen (3)
    XCTAssertEqual(frame[0], 0x83);
    // Sum of all but checksum equals checksum
    uint8_t sum = 0;
    for (size_t i = 0; i + 1 < frame.size(); ++i) { sum = static_cast<uint8_t>(sum + frame[i]); }
    XCTAssertEqual(sum, frame.back());
}

-(void)testBuildIso9141FrameMatchesSample {
    Bytes payload{ 0x41, 0x0D, 0x00 };
    auto frame = makeIso9141Frame(0x48, 0x6B, 0x11, payload);
    XCTAssertEqual(frame.size(), 3 + payload.size() + 1);
    // Check header positions and checksum against known trace (0x12)
    XCTAssertEqual(frame[0], 0x48);
    XCTAssertEqual(frame[1], 0x6B);
    XCTAssertEqual(frame[2], 0x11);
    XCTAssertEqual(frame.back(), 0x12);
}

-(void)testBuildKwpMultiFrameSequenceMatchesExistingFrames {
    // Base payload: service 0x49, PID 0x02, VIN data (20 bytes)
    Bytes payload{
        0x49, 0x02,
        0x00, 0x00, 0x00, 0x57, 0x44, 0x58, 0x2D, 0x53,
        0x49, 0x4D, 0x30, 0x30, 0x31, 0x39, 0x32, 0x31,
        0x32, 0x33, 0x34, 0x35
    };

    auto frames = makeKwpFrames(0xF1, 0x10, payload, 0x80, 4);
    XCTAssertEqual(frames.size(), 5);

    // Compare against the multi-frame example used in transceiver tests.
    std::vector<std::vector<uint8_t>> expected{
        { 0x87, 0xF1, 0x10, 0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x57, 0x00 },
        { 0x87, 0xF1, 0x10, 0x49, 0x02, 0x02, 0x44, 0x58, 0x2D, 0x53, 0x00 },
        { 0x87, 0xF1, 0x10, 0x49, 0x02, 0x03, 0x49, 0x4D, 0x30, 0x30, 0x00 },
        { 0x87, 0xF1, 0x10, 0x49, 0x02, 0x04, 0x31, 0x39, 0x32, 0x31, 0x00 },
        { 0x87, 0xF1, 0x10, 0x49, 0x02, 0x05, 0x32, 0x33, 0x34, 0x35, 0x00 },
    };
    for (auto& e : expected) {
        uint8_t sum = 0;
        for (size_t i = 0; i + 1 < e.size(); ++i) { sum = static_cast<uint8_t>(sum + e[i]); }
        e.back() = sum;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        XCTAssertEqual(frames[i], expected[i]);
    }
}

@end
