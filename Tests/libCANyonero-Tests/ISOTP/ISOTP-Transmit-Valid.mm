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

@interface ISOTP_Transmit_Valid : XCTestCase

@property(nonatomic,assign,readonly) Transceiver* isotp;

@end

@implementation ISOTP_Transmit_Valid

-(void)setUp {
    _isotp = new Transceiver(Transceiver::Behavior::strict, Transceiver::Mode::standard, 0, 0, 0);
}

-(void)tearDown {
    delete _isotp;
}

-(void)testSingleShort {
    auto message = std::vector<uint8_t> { 0x3E, 0x00 };
    auto action = _isotp->writePDU(message);
    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(action.frames.size(), 1);
    auto frame = action.frames[0];
    XCTAssertEqual(frame.bytes.size(), 8);
    XCTAssertEqual(frame.type(), Frame::Type::single);
    auto expected = std::vector<uint8_t>{ 0x02, 0x3E, 0x00, padding, padding, padding, padding, padding };
    XCTAssertEqual(frame.bytes, expected);
}

-(void)testSingleMax {
    auto message = std::vector<uint8_t> { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37 };
    auto action = _isotp->writePDU(message);
    XCTAssertEqual(action.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(action.frames.size(), 1);
    auto frame = action.frames[0];
    XCTAssertEqual(frame.bytes.size(), 8);
    XCTAssertEqual(frame.type(), Frame::Type::single);
    auto expected = std::vector<uint8_t>{ 0x07, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37 };
    XCTAssertEqual(frame.bytes, expected);
}

-(void)testFirstConsecutive {
    auto message = std::vector<uint8_t> { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };
    auto firstAction = _isotp->writePDU(message);
    XCTAssert(firstAction.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    auto frame = firstAction.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::first);
    XCTAssertEqual(frame.bytes.size(), 8);
    auto expected = std::vector<uint8_t> { 0x10, 0x08, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36 };
    XCTAssertEqual(frame.bytes, expected);

    auto flowControlBytes = std::vector<uint8_t> { 0x30, 0x00, 0x00, padding, padding, padding, padding, padding };
    auto secondAction = _isotp->didReceiveFrame(flowControlBytes);
    XCTAssertEqual(secondAction.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(secondAction.frames.size(), 1);
    auto frame2 = secondAction.frames[0];
    XCTAssertEqual(frame2.type(), Frame::Type::consecutive);
    XCTAssertEqual(frame2.bytes.size(), 8);
    auto expected2 = std::vector<uint8_t> { 0x21, 0x37, 0x38, padding, padding, padding, padding, padding };
    XCTAssertEqual(frame2.bytes, expected2);
}

-(void)testFirstWaitWaitClearConsecutive {
    auto message = std::vector<uint8_t> { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };
    auto firstAction = _isotp->writePDU(message);
    XCTAssert(firstAction.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    auto frame = firstAction.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::first);
    XCTAssertEqual(frame.bytes.size(), 8);
    auto expected = std::vector<uint8_t> { 0x10, 0x08, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36 };
    XCTAssertEqual(frame.bytes, expected);
    
    auto fcWait1 = std::vector<uint8_t> { 0x31, 0x00, 0x00, padding, padding, padding, padding, padding };
    auto waitAction1 = _isotp->didReceiveFrame(fcWait1);
    XCTAssertEqual(waitAction1.type, Transceiver::Action::Type::waitForMore);
    
    auto fcWait2 = std::vector<uint8_t> { 0x31, 0x00, 0x00, padding, padding, padding, padding, padding };
    auto waitAction2 = _isotp->didReceiveFrame(fcWait2);
    XCTAssertEqual(waitAction2.type, Transceiver::Action::Type::waitForMore);

    auto clearToSend = std::vector<uint8_t> { 0x30, 0x00, 0x00, padding, padding, padding, padding, padding };
    auto secondAction = _isotp->didReceiveFrame(clearToSend);
    XCTAssertEqual(secondAction.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(secondAction.frames.size(), 1);
    auto frame2 = secondAction.frames[0];
    XCTAssertEqual(frame2.type(), Frame::Type::consecutive);
    XCTAssertEqual(frame2.bytes.size(), 8);
    auto expected2 = std::vector<uint8_t> { 0x21, 0x37, 0x38, padding, padding, padding, padding, padding };
    XCTAssertEqual(frame2.bytes, expected2);
}

-(void)testMaxPayloadNoFlowControl {
    std::vector<uint8_t> pdu(maximumTransferSize);
    std::iota(pdu.begin(), pdu.end(), 0);

    auto firstAction = _isotp->writePDU(pdu);
    XCTAssert(firstAction.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    auto frame = firstAction.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::first);
    XCTAssertEqual(frame.bytes.size(), 8);
    auto expected = std::vector<uint8_t> { 0x1F, 0xFF } + vector_drop_first(pdu, 6);
    XCTAssertEqual(frame.bytes, expected);

    auto flowControlBytes = std::vector<uint8_t> { 0x30, 0x00, 0x00, padding, padding, padding, padding, padding };
    auto secondAction = _isotp->didReceiveFrame(flowControlBytes);
    XCTAssertEqual(secondAction.type, Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(secondAction.frames.size(), 585);

    uint8_t sequenceNumber = 0x01;
    for (auto& consecutive: secondAction.frames) {
        auto chunkSize = std::min(size_t(7), pdu.size());
        auto expected = std::vector<uint8_t>(1, 0x20 + sequenceNumber) + vector_drop_first(pdu, chunkSize);
        if (expected.size() < 8) {
            expected.resize(8, padding);
        }
        XCTAssertEqual(consecutive.type(), Frame::Type::consecutive);
        XCTAssertEqual(consecutive.bytes.size(), 8);
        XCTAssertEqual(consecutive.bytes, expected);
        sequenceNumber = (sequenceNumber + 1) & 0x0F;
    }

    XCTAssert(pdu.empty());
}

-(void)testMaxPayloadWithFlowControl {
    std::vector<uint8_t> pdu(maximumTransferSize);
    std::iota(pdu.begin(), pdu.end(), 0);

    auto firstAction = _isotp->writePDU(pdu);
    XCTAssert(firstAction.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    auto frame = firstAction.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::first);
    XCTAssertEqual(frame.bytes.size(), 8);
    auto expected = std::vector<uint8_t> { 0x1F, 0xFF } + vector_drop_first(pdu, 6);
    XCTAssertEqual(frame.bytes, expected);

    while (!pdu.empty()) {
        auto flowControlBytes = std::vector<uint8_t> { 0x30, 0x20, 0x00, padding, padding, padding, padding, padding };
        auto secondAction = _isotp->didReceiveFrame(flowControlBytes);
        XCTAssertEqual(secondAction.type, Transceiver::Action::Type::writeFrames);
        XCTAssertTrue(secondAction.frames.size() <= 0x20);

        uint8_t sequenceNumber = 0x01;
        for (auto& consecutive: secondAction.frames) {
            auto chunkSize = std::min(size_t(7), pdu.size());
            auto expected = std::vector<uint8_t>(1, 0x20 + sequenceNumber) + vector_drop_first(pdu, chunkSize);
            if (expected.size() < 8) {
                expected.resize(8, padding);
            }
            XCTAssertEqual(consecutive.type(), Frame::Type::consecutive);
            XCTAssertEqual(consecutive.bytes.size(), 8);
            XCTAssertEqual(consecutive.bytes, expected);
            sequenceNumber = (sequenceNumber + 1) & 0x0F;
        }
    }
}

-(void)testMaxPayloadWithFlowControlWaitClear {
    std::vector<uint8_t> pdu(maximumTransferSize);
    std::iota(pdu.begin(), pdu.end(), 0);
    
    auto firstAction = _isotp->writePDU(pdu);
    XCTAssert(firstAction.type == Transceiver::Action::Type::writeFrames);
    XCTAssertEqual(firstAction.frames.size(), 1);
    auto frame = firstAction.frames[0];
    XCTAssertEqual(frame.type(), Frame::Type::first);
    XCTAssertEqual(frame.bytes.size(), 8);
    auto expected = std::vector<uint8_t> { 0x1F, 0xFF } + vector_drop_first(pdu, 6);
    XCTAssertEqual(frame.bytes, expected);
    
    while (!pdu.empty()) {
        
        auto fcWait = std::vector<uint8_t> { 0x31, 0x00, 0x00, padding, padding, padding, padding, padding };
        auto waitAction = _isotp->didReceiveFrame(fcWait);
        XCTAssertEqual(waitAction.type, Transceiver::Action::Type::waitForMore);
        
        auto clearToSend0x20 = std::vector<uint8_t> { 0x30, 0x20, 0x00, padding, padding, padding, padding, padding };
        auto secondAction = _isotp->didReceiveFrame(clearToSend0x20);
        XCTAssertEqual(secondAction.type, Transceiver::Action::Type::writeFrames);
        XCTAssertTrue(secondAction.frames.size() <= 0x20);
        
        uint8_t sequenceNumber = 0x01;
        for (auto& consecutive: secondAction.frames) {
            auto chunkSize = std::min(size_t(7), pdu.size());
            auto expected = std::vector<uint8_t>(1, 0x20 + sequenceNumber) + vector_drop_first(pdu, chunkSize);
            if (expected.size() < 8) {
                expected.resize(8, padding);
            }
            XCTAssertEqual(consecutive.type(), Frame::Type::consecutive);
            XCTAssertEqual(consecutive.bytes.size(), 8);
            XCTAssertEqual(consecutive.bytes, expected);
            sequenceNumber = (sequenceNumber + 1) & 0x0F;
        }
    }
}

@end
