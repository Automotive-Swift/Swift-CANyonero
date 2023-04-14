///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>

#import "ISOTP.hpp"

using namespace CANyonero::ISOTP;

@interface ISOTP : XCTestCase

@property(nonatomic,assign,readonly) Transceiver* isotp;

@end

@implementation ISOTP

-(void)setUp {
    _isotp = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 0);
}

-(void)tearDown {
    delete _isotp;
}

-(void)testSingleShort {
    auto message = std::vector<uint8_t> { 0x3E, 0x00 };
    auto action = _isotp->writePDU(message);
    XCTAssert(action.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(action.frames.size(), 1);
    auto frame = action.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::single);
    auto expected = std::vector<uint8_t>{ 0x02, 0x3E, 0x00 };
    XCTAssertEqual(frame.bytes, expected);
}

-(void)testSingleMax {
    auto message = std::vector<uint8_t> { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37 };
    auto action = _isotp->writePDU(message);
    XCTAssert(action.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(action.frames.size(), 1);
    auto frame = action.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::single);
    auto expected = std::vector<uint8_t>{ 0x07, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37 };
    XCTAssertEqual(frame.bytes, expected);
}


-(void)testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end
