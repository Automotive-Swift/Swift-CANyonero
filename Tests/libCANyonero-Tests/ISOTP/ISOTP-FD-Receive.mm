///
/// CANyonero. (C) 2022 - 2026 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <algorithm>
#import <numeric>
#import <vector>
#import <iostream>

#import "ISOTP.hpp"

using namespace CANyonero::ISOTP;

@interface ISOTP_FD_Receive : XCTestCase
@end

@implementation ISOTP_FD_Receive

-(void)testReceiveSingleFrameFD {
    // Width = 64
    auto isotp = Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 0, 64);
    
    // Construct a frame with escape sequence
    std::vector<uint8_t> frameBytes(64, padding);
    frameBytes[0] = 0x00; // Type single, length 0
    frameBytes[1] = 10;   // Length 10
    for (uint8_t i = 0; i < 10; ++i) {
        frameBytes[i+2] = i;
    }
    
    auto action = isotp.didReceiveFrame(frameBytes);
    XCTAssertEqual(action.type, Transceiver::Action::Type::process);
    XCTAssertEqual(action.data.size(), 10);
    for (uint8_t i = 0; i < 10; ++i) {
        XCTAssertEqual(action.data[i], i);
    }
}

@end
