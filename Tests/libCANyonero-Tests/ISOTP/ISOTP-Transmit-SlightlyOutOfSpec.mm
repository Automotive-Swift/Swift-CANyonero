///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <algorithm>
#import <numeric>
#import <vector>
#import <iostream>

#import "ISOTP.hpp"

using namespace CANyonero::ISOTP;

@interface ISOTP_Transmit_SlightlyOutOfSpec : XCTestCase

@property(nonatomic,assign,readonly) Transceiver* isotp;

@end

@implementation ISOTP_Transmit_SlightlyOutOfSpec

-(void)setUp {
    _isotp = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 0);
}

-(void)tearDown {
    delete _isotp;
}

-(void)testFirstConsecutiveWithUnpaddedFlowControl {
    auto message = std::vector<uint8_t> { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };
    auto firstAction = _isotp->writePDU(message);
    XCTAssert(firstAction.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    auto frame = firstAction.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::first);
    XCTAssertEqual(frame.bytes.size(), 8);
    auto expected = std::vector<uint8_t> { 0x10, 0x08, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36 };
    XCTAssertEqual(frame.bytes, expected);

    auto flowControlBytes = std::vector<uint8_t> { 0x30, 0x00, 0x00 };
    auto secondAction = _isotp->didReceiveFrame(flowControlBytes);
    XCTAssertEqual(secondAction.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(secondAction.frames.size(), 1);
    auto frame2 = secondAction.frames[0];
    XCTAssertEqual(frame2.type(), Frame::Type::consecutive);
    XCTAssertEqual(frame2.bytes.size(), 8);
    auto expected2 = std::vector<uint8_t> { 0x21, 0x37, 0x38, padding, padding, padding, padding, padding };
    XCTAssertEqual(frame2.bytes, expected2);
}

-(void)testFlowControlTooShort {
    auto message = std::vector<uint8_t> { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };
    auto firstAction = _isotp->writePDU(message);
    XCTAssert(firstAction.type == Transceiver::Action::Type::writeFrames);

    auto invalidFlowControl = std::vector<uint8_t> { 0x30, 0x00 };
    auto response = _isotp->didReceiveFrame(invalidFlowControl);
    XCTAssertEqual(response.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testExtendedAddressingFlowControlWithShortDLC {
    auto isotpEA = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::extended, 0, 0, 0);

    auto message = std::vector<uint8_t> { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };
    auto firstAction = isotpEA->writePDU(message);
    XCTAssert(firstAction.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    auto frame = firstAction.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::first);
    XCTAssertEqual(frame.bytes.size(), 7);
    auto expected = std::vector<uint8_t> { 0x10, 0x08, 0x31, 0x32, 0x33, 0x34, 0x35 };
    XCTAssertEqual(frame.bytes, expected);

    auto flowControlBytes = std::vector<uint8_t> { 0x30, 0x00, 0x00 };
    auto secondAction = isotpEA->didReceiveFrame(flowControlBytes);
    XCTAssertEqual(secondAction.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(secondAction.frames.size(), 1);
    auto frame2 = secondAction.frames[0];
    XCTAssertEqual(frame2.type(), Frame::Type::consecutive);
    XCTAssertEqual(frame2.bytes.size(), 7);
    auto expected2 = std::vector<uint8_t> { 0x21, 0x36, 0x37, 0x38, padding, padding, padding };
    XCTAssertEqual(frame2.bytes, expected2);

    delete isotpEA;
}

@end
