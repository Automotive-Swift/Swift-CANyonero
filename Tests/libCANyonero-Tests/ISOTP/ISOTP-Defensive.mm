///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <vector>

#import "ISOTP.hpp"

using namespace CANyonero::ISOTP;

@interface ISOTP_Defensive : XCTestCase

@property(nonatomic,assign,readonly) Transceiver* isotp;

@end

@implementation ISOTP_Defensive

-(void)setUp {
    _isotp = new Transceiver(Transceiver::Behavior::defensive, Transceiver::Mode::standard, 0, 0, 500);
}

-(void)tearDown {
    delete _isotp;
}

-(void)testDefensiveTreatsUnexpectedSingleAsDataWhileSending {
    auto payload = std::vector<uint8_t>{ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90 };
    auto firstAction = _isotp->writePDU(payload);
    XCTAssertEqual(firstAction.type, Transceiver::Action::Type::writeFrames);

    auto single = std::vector<uint8_t>{ 0x02, 0xAA, 0xBB };
    auto response = _isotp->didReceiveFrame(single);
    XCTAssertEqual(response.type, Transceiver::Action::Type::process);
    std::vector<uint8_t> expected { 0xAA, 0xBB };
    XCTAssertEqual(response.data, expected);
}

-(void)testDefensiveKeepsWaitingForInvalidFrames {
    auto payload = std::vector<uint8_t>{ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90 };
    auto firstAction = _isotp->writePDU(payload);
    XCTAssertEqual(firstAction.type, Transceiver::Action::Type::writeFrames);

    auto invalid = std::vector<uint8_t>{ 0xFF };
    auto response = _isotp->didReceiveFrame(invalid);
    XCTAssertEqual(response.type, Transceiver::Action::Type::waitForMore);
}

-(void)testSeparationTimeHonorsConfiguredMaximum {
    auto transmitter = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 600);
    auto payload = std::vector<uint8_t>{ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90 };
    auto firstAction = transmitter->writePDU(payload);
    XCTAssertEqual(firstAction.type, Transceiver::Action::Type::writeFrames);

    auto flowControl = Frame::flowControl(Frame::FlowStatus::clearToSend, 0x00, 0xF1, 8);
    auto response = transmitter->didReceiveFrame(flowControl.bytes);
    XCTAssertEqual(response.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(response.separationTime, 600);
    delete transmitter;
}

-(void)testSeparationTimeTakesFlowControlWhenHigher {
    auto transmitter = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 100);
    auto payload = std::vector<uint8_t>{ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90 };
    auto firstAction = transmitter->writePDU(payload);
    XCTAssertEqual(firstAction.type, Transceiver::Action::Type::writeFrames);

    auto flowControl = Frame::flowControl(Frame::FlowStatus::clearToSend, 0x00, 0xF8, 8);
    auto response = transmitter->didReceiveFrame(flowControl.bytes);
    XCTAssertEqual(response.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(response.separationTime, 800);
    delete transmitter;
}

@end
