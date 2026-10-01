///
/// CANyonero. (C) 2022 - 2026 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import "ISOTPFD.hpp"

using namespace CANyonero::ISOTP;

namespace {
template<typename Sender>
void checkTransmitTimes(XCTestCase *self, Sender& sender, uint8_t width, uint8_t code, uint32_t expected) {
    auto first = sender.writePDU(std::vector<uint8_t>(width * 2, 0x55));
    XCTAssertEqual(first.type, Transceiver::Action::Type::writeFrames);
    // BS=1 forces two separate FC windows; both must preserve the full delay.
    for (int block = 0; block < 2; ++block) {
        auto fc = std::vector<uint8_t>{0x30, 1, code};
        fc.resize(width > 8 ? (width == 63 ? 7 : 8) : width, padding);
        size_t count = 0;
        auto action = sender.didReceiveFrameStreaming(fc, [&](Frame&& frame, uint32_t time, bool more) {
            ++count;
            XCTAssertEqual(time, expected, @"STmin 0x%02X", code);
            XCTAssertEqual(frame.type(), Frame::Type::consecutive);
            XCTAssertFalse(more);
        });
        XCTAssertEqual(count, 1);
        XCTAssertEqual(action.separationTime, expected, @"Action STmin 0x%02X", code);
    }
}
}

@interface ISOTP_Transmit_SeparationTime : XCTestCase
@end

@implementation ISOTP_Transmit_SeparationTime
-(void)testEveryLegalSTminPreservesMicrosecondsThroughClassicAndFDTransmit {
    for (unsigned code = 0; code <= 0xF9; ++code) {
        if (code > 0x7F && code < 0xF1) { continue; }
        const uint32_t expected = code <= 0x7F ? code * 1000 : (code - 0xF0) * 100;
        XCTAssertEqual(Frame::separationTimeToMicroseconds(code), expected);
        for (auto mode : {Transceiver::Mode::standard, Transceiver::Mode::extended}) {
            auto classic = Transceiver(Transceiver::Behavior::strict, mode);
            checkTransmitTimes(self, classic, mode == Transceiver::Mode::standard ? 8 : 7, code, expected);
            auto fd = TransceiverFD(Transceiver::Behavior::strict, mode);
            checkTransmitTimes(self, fd, mode == Transceiver::Mode::standard ? 64 : 63, code, expected);
        }
    }
}
@end
