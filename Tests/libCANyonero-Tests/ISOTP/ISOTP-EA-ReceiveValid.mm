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

@interface ISOTP_EA_Receive_Valid : XCTestCase

@property(nonatomic,assign,readonly) Transceiver* isotp;

@end

@implementation ISOTP_EA_Receive_Valid

-(void)setUp {
    _isotp = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::extended, 0, 0, 0);
}

-(void)tearDown {
    delete _isotp;
}

-(void)testSingleShort {
    auto single = std::vector<uint8_t>{ 0x02, 0x3E, 0x00, padding, padding, padding, padding };
    auto action = _isotp->didReceiveFrame(single);
    XCTAssertEqual(action.type, Transceiver::Action::Type::process);
    auto expected = std::vector<uint8_t> { 0x3E, 0x00 };
    XCTAssertEqual(action.data, expected);
}

-(void)testSingleMax {
    auto single = std::vector<uint8_t>{ 0x06, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36 };
    auto action = _isotp->didReceiveFrame(single);
    XCTAssertEqual(action.type, Transceiver::Action::Type::process);
    auto expected = std::vector<uint8_t> { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36 };
    XCTAssertEqual(action.data, expected);
}

-(void)testFirstConsecutive {
    auto first = std::vector<uint8_t> { 0x10, 0x08, 0x31, 0x32, 0x33, 0x34, 0x35 };
    auto firstAction = _isotp->didReceiveFrame(first);
    XCTAssert(firstAction.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    auto frame = firstAction.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::flowControl);
    XCTAssertEqual(frame.bytes.size(), 7);
    auto expected = std::vector<uint8_t> { 0x30, 0x00, 0x00, padding, padding, padding, padding };
    XCTAssertEqual(frame.bytes, expected);
    
    auto consecutive = std::vector<uint8_t> { 0x21, 0x36, 0x37, 0x38, padding, padding, padding };
    auto secondAction = _isotp->didReceiveFrame(consecutive);
    XCTAssertEqual(secondAction.type, Transceiver::Action::Type::process);
    auto pdu = std::vector<uint8_t> { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };
    XCTAssertEqual(secondAction.data, pdu);
}

-(void)testFirstFrameLengthSeven {
    auto first = std::vector<uint8_t> { 0x10, 0x07, 0x62, 0xF1, 0x86, 0x03, 0x00 };
    auto firstAction = _isotp->didReceiveFrame(first);
    XCTAssertEqual(firstAction.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    auto frame = firstAction.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::flowControl);
    XCTAssertEqual(frame.bytes.size(), 7);
    auto expected = std::vector<uint8_t> { 0x30, 0x00, 0x00, padding, padding, padding, padding };
    XCTAssertEqual(frame.bytes, expected);

    auto consecutive = std::vector<uint8_t> { 0x21, 0x01, 0x02, padding, padding, padding, padding };
    auto secondAction = _isotp->didReceiveFrame(consecutive);
    XCTAssertEqual(secondAction.type, Transceiver::Action::Type::process);
    auto pdu = std::vector<uint8_t> { 0x62, 0xF1, 0x86, 0x03, 0x00, 0x01, 0x02 };
    XCTAssertEqual(secondAction.data, pdu);
}

-(void)testMaxPayloadNoFlowControl {
    std::vector<uint8_t> pdu(maximumTransferSize);
    std::iota(pdu.begin(), pdu.end(), 0);
    
    auto firstFrame = std::vector<uint8_t>{ 0x1F, 0xFF } + vector_drop_first(pdu, 5);
    auto action = _isotp->didReceiveFrame(firstFrame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(action.frames.size(), 1);
    auto flowControl = action.frames[0];
    
    XCTAssertEqual(flowControl.type(), Frame::Type::flowControl);
    XCTAssertEqual(flowControl.bytes.size(), 7);
    auto expected = std::vector<uint8_t> { 0x30, 0x00, 0x00, padding, padding, padding, padding };
    XCTAssertEqual(flowControl.bytes, expected);
    
    uint8_t sequenceNumber = 0x01;
    while (!pdu.empty()) {
        
        auto chunkSize = std::min(size_t(6), pdu.size());
        auto consecutive = std::vector<uint8_t>(1, 0x20 + sequenceNumber) + vector_drop_first(pdu, chunkSize);
        consecutive.resize(7, padding);
        auto consecutiveAction = _isotp->didReceiveFrame(consecutive);
        sequenceNumber = (sequenceNumber + 1) & 0x0F;
        if (!pdu.empty()) {
            XCTAssertEqual(consecutiveAction.type, Transceiver::Action::Type::waitForMore);
        } else {
            XCTAssertEqual(consecutiveAction.type, Transceiver::Action::Type::process);
            std::vector<uint8_t> pdu(maximumTransferSize);
            std::iota(pdu.begin(), pdu.end(), 0);
            XCTAssertEqual(consecutiveAction.data, pdu);
        }
    }
}

-(void)testMaxPayloadWithFlowControl {
    
    auto blockSize = 5;
    auto isotp = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::extended, blockSize, 0, 0);
    
    std::vector<uint8_t> pdu(maximumTransferSize);
    std::iota(pdu.begin(), pdu.end(), 0);
    
    auto firstFrame = std::vector<uint8_t>{ 0x1F, 0xFF } + vector_drop_first(pdu, 5);
    auto action = isotp->didReceiveFrame(firstFrame);
    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(action.frames.size(), 1);
    auto flowControl = action.frames[0];
    
    XCTAssertEqual(flowControl.type(), Frame::Type::flowControl);
    XCTAssertEqual(flowControl.bytes.size(), 7);
    auto expected = std::vector<uint8_t> { 0x30, uint8_t(blockSize), 0x00, padding, padding, padding, padding };
    XCTAssertEqual(flowControl.bytes, expected);
    
    uint8_t unconfirmedBlocks = blockSize;
    uint8_t sequenceNumber = 0x01;
    while (!pdu.empty()) {
        
        auto chunkSize = std::min(size_t(6), pdu.size());
        auto consecutive = std::vector<uint8_t>(1, 0x20 + sequenceNumber) + vector_drop_first(pdu, chunkSize);
        consecutive.resize(7, padding);
        auto consecutiveAction = isotp->didReceiveFrame(consecutive);
        sequenceNumber = (sequenceNumber + 1) & 0x0F;
        unconfirmedBlocks--;
        
        if (!pdu.empty()) {
            if (unconfirmedBlocks == 0) {
                XCTAssertEqual(consecutiveAction.type, Transceiver::Action::Type::writeFrames);
                XCTAssertEqual(flowControl.bytes.size(), 7);
                auto expected = std::vector<uint8_t> { 0x30, uint8_t(blockSize), 0x00, padding, padding, padding, padding };
                XCTAssertEqual(flowControl.bytes, expected);
                unconfirmedBlocks = blockSize;
                continue;
            } else {
                XCTAssertEqual(consecutiveAction.type, Transceiver::Action::Type::waitForMore);
            }
        } else {
            XCTAssertEqual(consecutiveAction.type, Transceiver::Action::Type::process);
            std::vector<uint8_t> pdu(maximumTransferSize);
            std::iota(pdu.begin(), pdu.end(), 0);
            XCTAssertEqual(consecutiveAction.data, pdu);
        }
    }
    delete isotp;
}

@end
