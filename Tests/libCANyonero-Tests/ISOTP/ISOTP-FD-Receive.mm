///
/// CANyonero. (C) 2022 - 2026 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <numeric>
#import <vector>

#import "ISOTPFD.hpp"

using namespace CANyonero::ISOTP;

static bool isValidCANFDLengthForStandard(size_t length) {
    if (length <= 8) { return true; }
    return length == 12 || length == 16 || length == 20 || length == 24 || length == 32 || length == 48 || length == 64;
}

static std::vector<uint8_t> payloadForLength(size_t length, uint8_t base = 0x00) {
    auto payload = std::vector<uint8_t>(length);
    std::iota(payload.begin(), payload.end(), base);
    return payload;
}

static std::vector<uint8_t> makeSingleFrame(size_t payloadLength, uint8_t base = 0x00) {
    auto payload = payloadForLength(payloadLength, base);
    std::vector<uint8_t> frame;
    if (payloadLength <= 7) {
        frame.push_back(static_cast<uint8_t>(payloadLength));
    } else {
        frame.push_back(0x00);
        frame.push_back(static_cast<uint8_t>(payloadLength));
    }
    frame.insert(frame.end(), payload.begin(), payload.end());

    size_t targetLength = frame.size();
    if (targetLength > 8 && targetLength < 12) { targetLength = 12; }
    else if (targetLength > 12 && targetLength < 16) { targetLength = 16; }
    else if (targetLength > 16 && targetLength < 20) { targetLength = 20; }
    else if (targetLength > 20 && targetLength < 24) { targetLength = 24; }
    else if (targetLength > 24 && targetLength < 32) { targetLength = 32; }
    else if (targetLength > 32 && targetLength < 48) { targetLength = 48; }
    else if (targetLength > 48 && targetLength < 64) { targetLength = 64; }
    frame.resize(targetLength, padding);
    return frame;
}

@interface ISOTP_FD_Receive : XCTestCase
@end

@implementation ISOTP_FD_Receive

-(void)testReceiveSingleFrameAcceptsAllDynamicPayloadSizes {
    auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 0, 0, 0, 64);

    for (size_t payloadLength = 1; payloadLength <= 62; ++payloadLength) {
        auto frame = makeSingleFrame(payloadLength, 0x20);
        auto action = isotp.didReceiveFrame(frame);
        XCTAssertEqual(action.type, TransceiverFD::Action::Type::process);
        XCTAssertEqual(action.data.size(), payloadLength);
        for (size_t i = 0; i < payloadLength; ++i) {
            XCTAssertEqual(action.data[i], static_cast<uint8_t>(0x20 + i));
        }
    }
}

-(void)testRejectInvalidCANFDLengthsInStandardMode {
    auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 0, 0, 0, 64);

    for (size_t length = 1; length <= 64; ++length) {
        if (isValidCANFDLengthForStandard(length)) { continue; }
        auto frame = std::vector<uint8_t>(length, padding);
        frame[0] = 0x01;
        auto action = isotp.didReceiveFrame(frame);
        XCTAssertEqual(action.type, TransceiverFD::Action::Type::protocolViolation);
    }

    auto oversized = std::vector<uint8_t>(65, padding);
    oversized[0] = 0x01;
    auto oversizedAction = isotp.didReceiveFrame(oversized);
    XCTAssertEqual(oversizedAction.type, TransceiverFD::Action::Type::protocolViolation);
}

-(void)testReceiveMultiFrameWithDynamicConsecutiveWidths {
    auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 0, 0, 0, 64);
    auto pdu = payloadForLength(100, 0x40);

    auto first = std::vector<uint8_t> { 0x10, 0x64 };
    first.insert(first.end(), pdu.begin(), pdu.begin() + 10);
    first.resize(12, padding);

    auto firstAction = isotp.didReceiveFrame(first);
    XCTAssertEqual(firstAction.type, TransceiverFD::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    XCTAssertEqual(firstAction.frames[0].type(), Frame::Type::flowControl);
    XCTAssertEqual(firstAction.frames[0].bytes.size(), 3);
    XCTAssertEqual(firstAction.frames[0].bytes[0], 0x30);

    auto offset = size_t(10);
    uint8_t sn = 1;
    while (offset < pdu.size()) {
        auto remaining = pdu.size() - offset;
        auto chunk = std::min<size_t>(11, remaining);

        auto cf = std::vector<uint8_t> { static_cast<uint8_t>(0x20 | sn) };
        cf.insert(cf.end(), pdu.begin() + static_cast<std::ptrdiff_t>(offset), pdu.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        if (cf.size() > 8 && cf.size() < 12) { cf.resize(12, padding); }

        auto action = isotp.didReceiveFrame(cf);
        offset += chunk;
        sn = static_cast<uint8_t>((sn + 1) & 0x0F);

        if (offset < pdu.size()) {
            XCTAssertEqual(action.type, TransceiverFD::Action::Type::waitForMore);
        } else {
            XCTAssertEqual(action.type, TransceiverFD::Action::Type::process);
            XCTAssertEqual(action.data, pdu);
        }
    }
}

-(void)testFlowControlReplyUsesShortestValidFrame {
    auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 5, 200, 0, 64);
    auto pdu = payloadForLength(30, 0x60);

    auto first = std::vector<uint8_t> { 0x10, 0x1E };
    first.insert(first.end(), pdu.begin(), pdu.begin() + 10);
    first.resize(12, padding);

    auto action = isotp.didReceiveFrame(first);
    XCTAssertEqual(action.type, TransceiverFD::Action::Type::writeFrames);
    XCTAssertEqual(action.frames.size(), 1);
    XCTAssertEqual(action.frames[0].type(), Frame::Type::flowControl);
    XCTAssertEqual(action.frames[0].bytes.size(), 3);
    XCTAssertEqual(action.frames[0].bytes[0], 0x30);
    XCTAssertEqual(action.frames[0].bytes[1], 5);
    XCTAssertEqual(action.frames[0].bytes[2], 200);
}

-(void)testExtendedAddressingLengthValidation {
    auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::extended, 0, 0, 0, 63);

    auto invalid = std::vector<uint8_t>(8, padding); // effective width 8 => physical 9 (invalid for CAN-FD)
    invalid[0] = 0x01;
    auto invalidAction = isotp.didReceiveFrame(invalid);
    XCTAssertEqual(invalidAction.type, TransceiverFD::Action::Type::protocolViolation);

    auto valid = std::vector<uint8_t> { 0x02, 0x12, 0x34 };
    valid.resize(7, padding);
    auto validAction = isotp.didReceiveFrame(valid);
    XCTAssertEqual(validAction.type, TransceiverFD::Action::Type::process);
    auto expected = std::vector<uint8_t> { 0x12, 0x34 };
    XCTAssertEqual(validAction.data, expected);
}

@end
