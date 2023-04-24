///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>
#import <Foundation/Foundation.h>

#import <random>
#import <vector>

#import "ISOTP.hpp"

std::vector<uint8_t> generate_random_vector() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);

    std::vector<uint8_t> vec;
    uint8_t length = dis(gen) % 9;  // Generate a random length between 0 and 8
    for (int i = 0; i < length; i++) {
        vec.push_back(dis(gen));    // Generate a random byte and add it to the vector
    }
    return vec;
}

using namespace CANyonero::ISOTP;

@interface ISOTP_Receive_Invalid_Misc : XCTestCase

@property(nonatomic,assign,readonly) Transceiver* isotp;

@end

@implementation ISOTP_Receive_Invalid_Misc

-(void)setUp {
    _isotp = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 0);
}

-(void)tearDown {
    delete _isotp;
}

-(void)testUnexpectedFlowControlClear {
    auto consecutive = std::vector<uint8_t> { 0x30, 0x00, 0x00, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(consecutive);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testUnexpectedFlowControlWait {
    auto consecutive = std::vector<uint8_t> { 0x31, 0x00, 0x00, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(consecutive);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testUnexpectedFlowControlOverflow {
    auto consecutive = std::vector<uint8_t> { 0x32, 0x00, 0x00, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(consecutive);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testConsecutiveWithoutFirst {
    auto consecutive = std::vector<uint8_t> { 0x21, 0x07, padding, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(consecutive);
    XCTAssertEqual(action.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testBrokenSequenceNumbers {
    auto first = std::vector<uint8_t> { 0x10, 0xFF, padding, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(first);
    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);

    auto consecutive1 = std::vector<uint8_t> { 0x21, padding, padding, padding, padding, padding, padding, padding };
    auto action2 = _isotp->didReceiveFrame(consecutive1);
    XCTAssertEqual(action2.type, Transceiver::Action::Type::waitForMore);
    auto consecutive2 = std::vector<uint8_t> { 0x23, padding, padding, padding, padding, padding, padding, padding };
    auto action3 = _isotp->didReceiveFrame(consecutive2);
    XCTAssertEqual(action3.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testSingleAfterFirst {
    auto first = std::vector<uint8_t> { 0x10, 0xFF, padding, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(first);
    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);

    auto single = std::vector<uint8_t>{ 0x07, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37 };
    auto action2 = _isotp->didReceiveFrame(single);
    XCTAssertEqual(action2.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testTwoFirst {
    auto first = std::vector<uint8_t> { 0x10, 0xFF, padding, padding, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(first);
    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);

    auto first2 = std::vector<uint8_t> { 0x10, 0xFF, padding, padding, padding, padding, padding, padding };
    auto action2 = _isotp->didReceiveFrame(first2);
    XCTAssertEqual(action2.type, Transceiver::Action::Type::protocolViolation);
}

-(void)testRandomSmoke {
    for (int i = 0; i < 1000000; ++i) {
        auto random = generate_random_vector();
        auto action = _isotp->didReceiveFrame(random);
    }
}

-(void)testLinearSmoke {
    for (int len = 0; len <= 8; ++len) {
        printf("Testing all frames with length %d...\n", len);
        for (int i = 0; i < (1 << (8 * len)); ++i) {
            std::vector<uint8_t> vector;
            for (int j = 0; j < len; ++j) {
                vector.push_back((i >> (8 * j)) & 0xFF);
            }
            //print_hex_vector(vector);
            auto action = _isotp->didReceiveFrame(vector);
        }
    }
}

@end
