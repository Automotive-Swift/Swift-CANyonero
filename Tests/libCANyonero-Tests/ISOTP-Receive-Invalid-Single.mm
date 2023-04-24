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

@interface ISOTP_Receive_Invalid_Single : XCTestCase

@property(nonatomic,assign,readonly) Transceiver* isotp;

@end

@implementation ISOTP_Receive_Invalid_Single

-(void)setUp {
    _isotp = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 0);
}

-(void)tearDown {
    delete _isotp;
}

-(void)testSingleEmpty {
    auto frame = std::vector<uint8_t> {};
    auto action = _isotp->didReceiveFrame(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testSingleNoPadding {
    auto frame = std::vector<uint8_t> { 0x01, 0x02, 0x03 };
    auto action = _isotp->didReceiveFrame(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testSingleExceedingWidth {
    auto frame = std::vector<uint8_t> { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
    auto action = _isotp->didReceiveFrame(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testSingleZeroPCI {
    auto frame = std::vector<uint8_t> { 0x00, padding, padding, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testSingleTooLargePCI {
    auto frame = std::vector<uint8_t> { 0x08, padding, padding, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testInvalidPCI {
    for (uint8_t pci = 0x31; pci < 0x00; ++pci) {
        auto frame = std::vector<uint8_t> { pci, padding, padding, padding, padding, padding, padding, padding };
        auto action = _isotp->didReceiveFrame(frame);
        XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
    }
}

@end
