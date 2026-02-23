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

@interface ISOTP_FD_Transmit : XCTestCase
@end

@implementation ISOTP_FD_Transmit

-(void)testSingleFrameFD {
    // Width = 64
    auto isotp = Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 0, 64);
    
    // Message length = 62 (max for SF FD with width 64)
    std::vector<uint8_t> message(62);
    std::iota(message.begin(), message.end(), 0x10);
    
    auto action = isotp.writePDU(message);
    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(action.frames.size(), 1);
    
    auto frame = action.frames[0];
    XCTAssertEqual(frame.bytes.size(), 64);
    XCTAssertEqual(frame.type(), Frame::Type::single);
    
    // Check PCI
    XCTAssertEqual(frame.bytes[0], 0x00); // PCI type single, length 0 (escape)
    XCTAssertEqual(frame.bytes[1], 62);   // Real length
    
    // Check data
    for (size_t i = 0; i < 62; ++i) {
        XCTAssertEqual(frame.bytes[i+2], static_cast<uint8_t>(0x10 + i));
    }
}

-(void)testSingleFrameFDSmall {
    // Width = 64
    auto isotp = Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 0, 64);
    
    // Message length = 5
    std::vector<uint8_t> message = { 0x11, 0x22, 0x33, 0x44, 0x55 };
    
    auto action = isotp.writePDU(message);
    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(action.frames.size(), 1);
    
    auto frame = action.frames[0];
    XCTAssertEqual(frame.bytes.size(), 64);
    XCTAssertEqual(frame.type(), Frame::Type::single);
    
    // For FD, even small frames use the escape sequence if width > 8?
    // Actually, looking at the code:
    /*
        if (width > standardFrameWidth) {
            // CAN-FD single-frame escape sequence: PCI nibble 0 and explicit SF length byte.
            vector = { uint8_t(Type::single), static_cast<uint8_t>(bytes.size()) };
        }
    */
    // Yes, the code always uses escape sequence for width > 8.
    
    XCTAssertEqual(frame.bytes[0], 0x00);
    XCTAssertEqual(frame.bytes[1], 5);
    XCTAssertEqual(frame.bytes[2], 0x11);
}

@end
