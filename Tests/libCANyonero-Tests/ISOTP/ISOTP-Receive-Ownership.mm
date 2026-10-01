#import <XCTest/XCTest.h>
#include "ISOTP.hpp"
#include "ISOTPFD.hpp"

using namespace CANyonero;
using namespace CANyonero::ISOTP;

@interface ISOTP_Receive_Ownership : XCTestCase
@end

@implementation ISOTP_Receive_Ownership

template <typename T>
static void checkRepeatedReceives(T& sender, T& receiver) {
    using Action = typename T::Action;
    std::vector<Bytes> retained;
    std::vector<Bytes> expected;
    for (size_t size : {4095u, 128u, 1024u, 4095u}) {
        Bytes payload(size);
        for (size_t i = 0; i < size; ++i) payload[i] = static_cast<uint8_t>(i * 37 + retained.size());
        auto first = sender.writePDU(payload);
        auto fc = receiver.didReceiveFrame(first.frames[0].bytes);
        XCTAssertEqual(fc.type, Action::Type::writeFrames);
        auto window = sender.didReceiveFrame(fc.frames[0].bytes);
        Action received;
        for (const auto& frame : window.frames) received = receiver.didReceiveFrame(frame.bytes);
        XCTAssertEqual(received.type, Action::Type::process);
        XCTAssertTrue(received.data == payload);
        retained.push_back(std::move(received.data));
        expected.push_back(std::move(payload));
        // A subsequent receive/reset must not invalidate an earlier delivered PDU.
        XCTAssertTrue(retained == expected);
    }
}

- (void)testClassicRepeatedReceiveKeepsDeliveredPayloads {
    Transceiver sender(Transceiver::Behavior::strict, Transceiver::Mode::standard);
    Transceiver receiver(Transceiver::Behavior::strict, Transceiver::Mode::standard);
    checkRepeatedReceives(sender, receiver);
}

- (void)testFDRepeatedReceiveKeepsDeliveredPayloads {
    TransceiverFD sender(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 0, 0, 0, 64);
    TransceiverFD receiver(TransceiverFD::Behavior::strict, TransceiverFD::Mode::standard, 0, 0, 0, 64);
    checkRepeatedReceives(sender, receiver);
}
@end
