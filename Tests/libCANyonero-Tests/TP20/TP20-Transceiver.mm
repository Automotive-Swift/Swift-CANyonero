#import <XCTest/XCTest.h>

#import "TP20.hpp"

using namespace CANyonero::TP20;

static void AssertBytesEqual(const CANyonero::Bytes& actual, std::initializer_list<uint8_t> expected) {
    XCTAssertEqual(actual.size(), expected.size());
    auto actualIt = actual.begin();
    auto expectedIt = expected.begin();
    for (; actualIt != actual.end() && expectedIt != expected.end(); ++actualIt, ++expectedIt) {
        XCTAssertEqual(*actualIt, *expectedIt);
    }
}

@interface TP20_Transceiver : XCTestCase
@end

@implementation TP20_Transceiver

- (void)testRequestConnectionEmitsSetupFrame {
    auto tp20 = Transceiver();
    using CANyonero::Bytes;

    auto action = tp20.requestConnection();

    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(action.event, Transceiver::Event::connectionRequested);
    XCTAssertEqual(action.frames.size(), static_cast<size_t>(1));
    AssertBytesEqual(action.frames.front().bytes, { 0xA0, 0x0F, 0xC4, 0xFF, 0x01, 0xFF });
    XCTAssertEqual(tp20.machineState(), Transceiver::State::awaitingConnectionAcknowledge);
}

- (void)testConnectionAcknowledgeConnectsAndSingleFrameSend {
    auto tp20 = Transceiver();
    using CANyonero::Bytes;
    (void) tp20.requestConnection();

    auto connectAction = tp20.didReceiveFrame(Bytes { 0xA1, 0x0F, 0x8A, 0xFF, 0x0A, 0xFF });
    XCTAssertEqual(connectAction.type, Transceiver::Action::Type::event);
    XCTAssertEqual(connectAction.event, Transceiver::Event::connected);
    XCTAssertTrue(tp20.isConnected());

    auto sendAction = tp20.writePDU(Bytes { 0x27, 0x01 });
    XCTAssertEqual(sendAction.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(sendAction.frames.size(), static_cast<size_t>(1));
    AssertBytesEqual(sendAction.frames.front().bytes, { 0x10, 0x00, 0x02, 0x27, 0x01 });

    auto ackAction = tp20.didReceiveFrame(Bytes { 0xB1 });
    XCTAssertEqual(ackAction.type, Transceiver::Action::Type::waitForMore);
    XCTAssertEqual(tp20.machineState(), Transceiver::State::connected);
}

- (void)testSubsequentPDUContinuesTransmitSequence {
    auto tp20 = Transceiver();
    using CANyonero::Bytes;
    (void) tp20.requestConnection();
    (void) tp20.didReceiveFrame(Bytes { 0xA1, 0x0F, 0x8A, 0xFF, 0x0A, 0xFF });

    auto firstSendAction = tp20.writePDU(Bytes { 0x27, 0x01 });
    XCTAssertEqual(firstSendAction.type, Transceiver::Action::Type::writeFrames);
    AssertBytesEqual(firstSendAction.frames.front().bytes, { 0x10, 0x00, 0x02, 0x27, 0x01 });

    auto firstAckAction = tp20.didReceiveFrame(Bytes { 0xB1 });
    XCTAssertEqual(firstAckAction.type, Transceiver::Action::Type::waitForMore);
    XCTAssertEqual(tp20.machineState(), Transceiver::State::connected);

    auto secondSendAction = tp20.writePDU(Bytes { 0x1A, 0x9B });
    XCTAssertEqual(secondSendAction.type, Transceiver::Action::Type::writeFrames);
    AssertBytesEqual(secondSendAction.frames.front().bytes, { 0x11, 0x00, 0x02, 0x1A, 0x9B });
    XCTAssertEqual(tp20.machineState(), Transceiver::State::sending);
}

- (void)testReceiveSingleFrameReturnsPayloadAndAck {
    auto tp20 = Transceiver();
    using CANyonero::Bytes;
    (void) tp20.requestConnection();
    (void) tp20.didReceiveFrame(Bytes { 0xA1, 0x0F, 0x8A, 0xFF, 0x0A, 0xFF });

    auto action = tp20.didReceiveFrame(Bytes { 0x10, 0x00, 0x02, 0x50, 0x85 });

    XCTAssertEqual(action.type, Transceiver::Action::Type::processAndWriteFrames);
    AssertBytesEqual(action.data, { 0x50, 0x85 });
    XCTAssertEqual(action.frames.size(), static_cast<size_t>(1));
    AssertBytesEqual(action.frames.front().bytes, { 0xB1 });
    XCTAssertEqual(tp20.machineState(), Transceiver::State::connected);
}

@end
