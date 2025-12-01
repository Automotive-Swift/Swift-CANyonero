///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <vector>

#import "KLine.hpp"

using namespace CANyonero::KLine;
using CANyonero::Bytes;

@interface KLine_Frame_Split : XCTestCase
@end

@implementation KLine_Frame_Split

-(void)testSplitKwpMultiFrameStream {
    // Concatenate the VIN multi-frame example into a single buffer.
    std::vector<uint8_t> stream{
        0x87, 0xF1, 0x10, 0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x57, 0x00,
        0x87, 0xF1, 0x10, 0x49, 0x02, 0x02, 0x44, 0x58, 0x2D, 0x53, 0x00,
        0x87, 0xF1, 0x10, 0x49, 0x02, 0x03, 0x49, 0x4D, 0x30, 0x30, 0x00,
    };
    for (size_t i = 0; i < stream.size();) {
        uint8_t sum = 0;
        size_t frameLen = 3 + (stream[i] & 0x0F) + 1;
        for (size_t j = 0; j + 1 < frameLen; ++j) { sum = static_cast<uint8_t>(sum + stream[i + j]); }
        stream[i + frameLen - 1] = sum;
        i += frameLen;
    }

    auto frames = splitFrames(stream.data(), stream.size(), ProtocolMode::kwp);
    XCTAssertEqual(frames.size(), 3);

    auto decoded = decodeStream(stream, ProtocolMode::kwp, 0xF1, 0x10);
    std::vector<uint8_t> expected{
        0x49, 0x02, 0x00, 0x00, 0x00, 0x57,
        0x44, 0x58, 0x2D, 0x53, 0x49, 0x4D, 0x30, 0x30
    };
    XCTAssertEqual(decoded, expected);
}

-(void)testSplitIsoTreatsEntireBufferAsFrame {
    std::vector<uint8_t> frame{ 0x48, 0x6B, 0x11, 0x41, 0x0D, 0x00, 0x12 };
    auto frames = splitFrames(frame.data(), frame.size(), ProtocolMode::iso9141);
    XCTAssertEqual(frames.size(), 1);
    XCTAssertEqual(frames[0], frame);
}

@end
