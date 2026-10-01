///
/// CANyonero. (C) 2022 - 2026 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <numeric>
#import "ISOTPFD.hpp"

using namespace CANyonero::ISOTP;

namespace {
struct SeparationTimeCase {
    uint16_t microseconds;
    uint8_t encoded;
};

// Every receive separation time supported by the ECUconnect wire protocol.
constexpr SeparationTimeCase separationTimes[] = {
    {0, 0x00}, {100, 0xF1}, {200, 0xF2}, {300, 0xF3},
    {400, 0xF4}, {500, 0xF5}, {600, 0xF6}, {700, 0xF7},
    {800, 0xF8}, {900, 0xF9}, {1000, 0x01}, {2000, 0x02},
    {3000, 0x03}, {4000, 0x04}, {5000, 0x05}, {6000, 0x06},
};

template<typename Receiver>
void checkReception(XCTestCase *self, Receiver& receiver, uint8_t width,
                    uint8_t flowControlWidth, SeparationTimeCase separationTime) {
    // One first frame and two consecutive frames force a second FC with BS=1.
    auto payload = std::vector<uint8_t>(2 * width);
    std::iota(payload.begin(), payload.end(), 0x40);
    auto first = std::vector<uint8_t>{0x10, static_cast<uint8_t>(payload.size())};
    first.insert(first.end(), payload.begin(), payload.begin() + width - 2);

    auto checkFlowControl = [&](const Transceiver::Action& action) {
        XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);
        XCTAssertEqual(action.frames.size(), 1);
        if (action.frames.size() != 1) { return; }
        auto expected = std::vector<uint8_t>{0x30, 0x01, separationTime.encoded};
        expected.resize(flowControlWidth, padding);
        XCTAssertEqual(action.frames[0].bytes, expected,
                       @"RX %u us must encode as 0x%02X", separationTime.microseconds, separationTime.encoded);
    };

    checkFlowControl(receiver.didReceiveFrame(first));
    auto offset = static_cast<size_t>(width - 2);
    auto consecutive = std::vector<uint8_t>{0x21};
    consecutive.insert(consecutive.end(), payload.begin() + offset, payload.begin() + offset + width - 1);
    checkFlowControl(receiver.didReceiveFrame(consecutive));

    offset += width - 1;
    auto last = std::vector<uint8_t>{0x22};
    last.insert(last.end(), payload.begin() + offset, payload.end());
    last.resize(width, padding);
    auto completion = receiver.didReceiveFrame(last);
    XCTAssertEqual(completion.type, Transceiver::Action::Type::process);
    XCTAssertEqual(completion.data, payload);
}
}

@interface ISOTP_Receive_SeparationTime : XCTestCase
@end

@implementation ISOTP_Receive_SeparationTime

-(void)testClassicFlowControlEncodesMicrosecondsOnFirstAndSubsequentBlocks {
    for (auto mode : {Transceiver::Mode::standard, Transceiver::Mode::extended}) {
        const uint8_t width = mode == Transceiver::Mode::standard ? 8 : 7;
        for (auto separationTime : separationTimes) {
            auto receiver = Transceiver(Transceiver::Behavior::strict, mode, 1, separationTime.microseconds, 0);
            checkReception(self, receiver, width, width, separationTime);
        }
    }
}

-(void)testCANFDFlowControlEncodesMicrosecondsOnFirstAndSubsequentBlocks {
    for (auto mode : {Transceiver::Mode::standard, Transceiver::Mode::extended}) {
        const uint8_t width = mode == Transceiver::Mode::standard ? 64 : 63;
        const uint8_t flowControlWidth = mode == Transceiver::Mode::standard ? 8 : 7;
        for (auto separationTime : separationTimes) {
            auto receiver = TransceiverFD(Transceiver::Behavior::strict, mode, 1, separationTime.microseconds, 0, width);
            checkReception(self, receiver, width, flowControlWidth, separationTime);
        }
    }
}
@end
