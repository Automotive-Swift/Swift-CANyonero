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

@interface ISOTP_Receive_Invalid_First : XCTestCase

@property(nonatomic,assign,readonly) Transceiver* isotp;

@end

@implementation ISOTP_Receive_Invalid_First

-(void)setUp {
    _isotp = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 0);
}

-(void)tearDown {
    delete _isotp;
}

-(void)testFirstTooSmall {
    auto frame = std::vector<uint8_t> { 0x10, 0x07, padding, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(frame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

@end
