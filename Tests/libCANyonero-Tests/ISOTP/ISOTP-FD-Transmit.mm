///
/// CANyonero. (C) 2022 - 2026 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <numeric>
#import <vector>

#import "ISOTPFD.hpp"

using namespace CANyonero::ISOTP;

static size_t nextCANFDLength(size_t required) {
    if (required <= 8) { return required; }
    if (required <= 12) { return 12; }
    if (required <= 16) { return 16; }
    if (required <= 20) { return 20; }
    if (required <= 24) { return 24; }
    if (required <= 32) { return 32; }
    if (required <= 48) { return 48; }
    return 64;
}

static size_t nextCANFDLengthExtended(size_t required) {
    return nextCANFDLength(required + 1) - 1;
}

static size_t applyStandardMinimumDLC(size_t frameLength) {
    return frameLength < 8 ? 8 : frameLength;
}

static size_t applyExtendedMinimumDLC(size_t frameLength) {
    return frameLength < 7 ? 7 : frameLength;
}

static std::vector<uint8_t> payloadForLength(size_t length, uint8_t base = 0x10) {
    auto payload = std::vector<uint8_t>(length);
    std::iota(payload.begin(), payload.end(), base);
    return payload;
}

@interface ISOTP_FD_Transmit : XCTestCase
@end

@implementation ISOTP_FD_Transmit

-(void)testSingleFrameUsesDynamicDLCForAllPayloadSizes {
    auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 0, 0, 0, 64);

    for (size_t payloadLength = 1; payloadLength <= 62; ++payloadLength) {
        auto message = payloadForLength(payloadLength);
        auto action = isotp.writePDU(message);
        XCTAssertEqual(action.type, TransceiverFD::Action::Type::writeFrames);
        XCTAssertEqual(action.frames.size(), 1);

        auto frame = action.frames[0];
        XCTAssertEqual(frame.type(), Frame::Type::single);

        auto expectedLength = payloadLength <= 7
            ? nextCANFDLength(payloadLength + 1)
            : nextCANFDLength(payloadLength + 2);
        expectedLength = applyStandardMinimumDLC(expectedLength);
        XCTAssertEqual(frame.bytes.size(), expectedLength);

        if (payloadLength <= 7) {
            XCTAssertEqual(frame.bytes[0], payloadLength);
            for (size_t i = 0; i < payloadLength; ++i) {
                XCTAssertEqual(frame.bytes[i + 1], message[i]);
            }
            for (size_t i = payloadLength + 1; i < frame.bytes.size(); ++i) {
                XCTAssertEqual(frame.bytes[i], padding);
            }
        } else {
            XCTAssertEqual(frame.bytes[0], 0x00);
            XCTAssertEqual(frame.bytes[1], payloadLength);
            for (size_t i = 0; i < payloadLength; ++i) {
                XCTAssertEqual(frame.bytes[i + 2], message[i]);
            }
            for (size_t i = payloadLength + 2; i < frame.bytes.size(); ++i) {
                XCTAssertEqual(frame.bytes[i], padding);
            }
        }
        XCTAssertEqual(isotp.machineState(), TransceiverFD::State::idle);
    }
}

-(void)testMultiFrameLastConsecutiveUsesDynamicDLCForAllRemainders {
    for (size_t remainder = 1; remainder <= 63; ++remainder) {
        auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 0, 0, 0, 64);
        auto message = payloadForLength(62 + remainder);

        auto firstAction = isotp.writePDU(message);
        XCTAssertEqual(firstAction.type, TransceiverFD::Action::Type::writeFrames);
        XCTAssertEqual(firstAction.frames.size(), 1);
        XCTAssertEqual(firstAction.frames[0].type(), Frame::Type::first);
        XCTAssertEqual(firstAction.frames[0].bytes.size(), 64);

        auto flowControl = std::vector<uint8_t> { 0x30, 0x00, 0x00 };
        auto secondAction = isotp.didReceiveFrame(flowControl);
        XCTAssertEqual(secondAction.type, TransceiverFD::Action::Type::writeFrames);
        XCTAssertEqual(secondAction.frames.size(), 1);

        auto consecutive = secondAction.frames[0];
        XCTAssertEqual(consecutive.type(), Frame::Type::consecutive);
        XCTAssertEqual(consecutive.bytes[0], 0x21);
        auto expectedLength = applyStandardMinimumDLC(nextCANFDLength(1 + remainder));
        XCTAssertEqual(consecutive.bytes.size(), expectedLength);
        for (size_t i = 0; i < remainder; ++i) {
            XCTAssertEqual(consecutive.bytes[i + 1], message[62 + i]);
        }
        for (size_t i = remainder + 1; i < consecutive.bytes.size(); ++i) {
            XCTAssertEqual(consecutive.bytes[i], padding);
        }
        XCTAssertEqual(isotp.machineState(), TransceiverFD::State::idle);
    }
}

-(void)testBlockSizeOneUsesDynamicDLCAcrossMultipleConsecutives {
    for (size_t remainder = 1; remainder <= 63; ++remainder) {
        auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 0, 0, 0, 64);
        auto message = payloadForLength(62 + 63 + remainder, 0x40);

        auto firstAction = isotp.writePDU(message);
        XCTAssertEqual(firstAction.type, TransceiverFD::Action::Type::writeFrames);
        XCTAssertEqual(firstAction.frames.size(), 1);

        auto flowControl = std::vector<uint8_t> { 0x30, 0x01, 0x00 };
        auto secondAction = isotp.didReceiveFrame(flowControl);
        XCTAssertEqual(secondAction.type, TransceiverFD::Action::Type::writeFrames);
        XCTAssertEqual(secondAction.frames.size(), 1);
        XCTAssertEqual(secondAction.frames[0].bytes.size(), 64);
        XCTAssertEqual(secondAction.frames[0].bytes[0], 0x21);

        auto thirdAction = isotp.didReceiveFrame(flowControl);
        XCTAssertEqual(thirdAction.type, TransceiverFD::Action::Type::writeFrames);
        XCTAssertEqual(thirdAction.frames.size(), 1);
        XCTAssertEqual(thirdAction.frames[0].bytes[0], 0x22);
        auto expectedLength = applyStandardMinimumDLC(nextCANFDLength(1 + remainder));
        XCTAssertEqual(thirdAction.frames[0].bytes.size(), expectedLength);
        XCTAssertEqual(isotp.machineState(), TransceiverFD::State::idle);
    }
}

-(void)testExtendedAddressingUsesShiftedDynamicDLC {
    auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::extended, 0, 0, 0, 63);

    auto singlePayload = payloadForLength(8, 0x90);
    auto singleAction = isotp.writePDU(singlePayload);
    XCTAssertEqual(singleAction.type, TransceiverFD::Action::Type::writeFrames);
    XCTAssertEqual(singleAction.frames.size(), 1);
    XCTAssertEqual(singleAction.frames[0].type(), Frame::Type::single);
    XCTAssertEqual(singleAction.frames[0].bytes.size(), applyExtendedMinimumDLC(nextCANFDLengthExtended(10)));
    XCTAssertEqual(singleAction.frames[0].bytes[0], 0x00);
    XCTAssertEqual(singleAction.frames[0].bytes[1], 8);

    auto multiPayload = payloadForLength(62, 0x20);
    auto firstAction = isotp.writePDU(multiPayload);
    XCTAssertEqual(firstAction.type, TransceiverFD::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames[0].type(), Frame::Type::first);
    XCTAssertEqual(firstAction.frames[0].bytes.size(), 63);

    auto flowControl = std::vector<uint8_t> { 0x30, 0x00, 0x00 };
    auto secondAction = isotp.didReceiveFrame(flowControl);
    XCTAssertEqual(secondAction.type, TransceiverFD::Action::Type::writeFrames);
    XCTAssertEqual(secondAction.frames.size(), 1);
    XCTAssertEqual(secondAction.frames[0].type(), Frame::Type::consecutive);
    XCTAssertEqual(secondAction.frames[0].bytes.size(), applyExtendedMinimumDLC(nextCANFDLengthExtended(2)));
    XCTAssertEqual(secondAction.frames[0].bytes[0], 0x21);
}

-(void)testMinimumDLCZeroAllowsShortestFrames {
    auto isotp = TransceiverFD(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 0, 0, 0, 64, 0);

    auto singlePayload = payloadForLength(1, 0x11);
    auto singleAction = isotp.writePDU(singlePayload);
    XCTAssertEqual(singleAction.type, TransceiverFD::Action::Type::writeFrames);
    XCTAssertEqual(singleAction.frames.size(), 1);
    XCTAssertEqual(singleAction.frames[0].bytes.size(), 2);

    auto multiPayload = payloadForLength(63, 0x20);
    auto firstAction = isotp.writePDU(multiPayload);
    XCTAssertEqual(firstAction.type, TransceiverFD::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    XCTAssertEqual(firstAction.frames[0].bytes.size(), 64);

    auto flowControl = std::vector<uint8_t> { 0x30, 0x00, 0x00 };
    auto secondAction = isotp.didReceiveFrame(flowControl);
    XCTAssertEqual(secondAction.type, TransceiverFD::Action::Type::writeFrames);
    XCTAssertEqual(secondAction.frames.size(), 1);
    XCTAssertEqual(secondAction.frames[0].bytes.size(), 2);
}

@end
