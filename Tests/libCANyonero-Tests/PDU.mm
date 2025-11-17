///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#import <XCTest/XCTest.h>

#include <algorithm>
#include <string>

#import "Protocol.hpp"

using namespace CANyonero;

@interface PDU_Tests : XCTestCase
@end

@implementation PDU_Tests

- (void)testOpenChannelMetadata {
    auto pdu = CANyonero::PDU::openChannel(CANyonero::ChannelProtocol::isotp, 500000, 0x08, 0x02);

    XCTAssertEqual(pdu.type(), CANyonero::PDUType::openChannel);
    XCTAssertEqual(static_cast<uint8_t>(pdu.protocol()), static_cast<uint8_t>(CANyonero::ChannelProtocol::isotp));
    XCTAssertEqual(pdu.bitrate(), 500000u);

    auto separation = pdu.separationTimes();
    XCTAssertEqual(separation.first, 200u);
    XCTAssertEqual(separation.second, 2000u);
}

- (void)testCloseChannelUsesCorrectType {
    const CANyonero::ChannelHandle handle = 0xAA;
    auto pdu = CANyonero::PDU::closeChannel(handle);

    XCTAssertEqual(pdu.type(), CANyonero::PDUType::closeChannel);
    XCTAssertEqual(pdu.channel(), handle);
}

- (void)testSendCompressedRoundTrip {
    CANyonero::Bytes payload;
    for (uint8_t value = 0; value < 32; ++value) {
        payload.push_back(value);
    }

    auto pdu = CANyonero::PDU::sendCompressed(0x01, payload);
    XCTAssertEqual(pdu.type(), CANyonero::PDUType::sendCompressed);
    XCTAssertEqual(pdu.channel(), 0x01);
    XCTAssertEqual(pdu.uncompressedLength(), payload.size());

    auto uncompressed = pdu.uncompressedData();
    XCTAssertEqual(uncompressed.size(), payload.size());
    XCTAssertTrue(std::equal(uncompressed.begin(), uncompressed.end(), payload.begin()));
}

- (void)testReceivedCompressedRoundTrip {
    CANyonero::Bytes payload;
    for (int i = 0; i < 48; ++i) {
        payload.push_back(static_cast<uint8_t>(i * 3));
    }

    auto pdu = CANyonero::PDU::receivedCompressed(0x02, 0x12345678, 0x01, payload);
    XCTAssertEqual(pdu.type(), CANyonero::PDUType::receivedCompressed);
    XCTAssertEqual(pdu.channel(), 0x02);
    XCTAssertEqual(pdu.uncompressedLength(), payload.size());

    auto uncompressed = pdu.uncompressedData();
    XCTAssertEqual(uncompressed.size(), payload.size());
    XCTAssertTrue(std::equal(uncompressed.begin(), uncompressed.end(), payload.begin()));
}

- (void)testReceivedFrameDataExtraction {
    CANyonero::Bytes data = { 0x01, 0x02, 0x03, 0x04 };
    auto pdu = CANyonero::PDU::received(0x03, 0xABCDEF00, 0x01, data);
    XCTAssertEqual(pdu.type(), CANyonero::PDUType::received);
    XCTAssertEqual(pdu.channel(), 0x03);

    auto payload = pdu.data();
    XCTAssertEqual(payload.size(), data.size());
    XCTAssertTrue(std::equal(payload.begin(), payload.end(), data.begin()));
}

- (void)testContainsPDUScenarios {
    CANyonero::Bytes garbage = { 0x00, 0x00, 0x00 };
    XCTAssertEqual(CANyonero::PDU::containsPDU(garbage), -3);

    CANyonero::Bytes partial = { 0x1F, 0x10 };
    XCTAssertEqual(CANyonero::PDU::containsPDU(partial), 0);

    auto ping = CANyonero::PDU::ping({ 0xDE, 0xAD, 0xBE, 0xEF });
    auto frame = ping.frame();
    XCTAssertEqual(CANyonero::PDU::containsPDU(frame), frame.size());
}

- (void)testSetArbitrationRoundTrip {
    CANyonero::Arbitration arbitration;
    arbitration.request = 0x7DF;
    arbitration.requestExtension = 0x01;
    arbitration.replyPattern = 0x7E8;
    arbitration.replyMask = 0x7F8;
    arbitration.replyExtension = 0x02;

    auto pdu = CANyonero::PDU::setArbitration(0x05, arbitration);
    XCTAssertEqual(pdu.type(), CANyonero::PDUType::setArbitration);
    XCTAssertEqual(pdu.channel(), 0x05);

    auto parsed = pdu.arbitration();
    XCTAssertEqual(parsed.request, arbitration.request);
    XCTAssertEqual(parsed.requestExtension, arbitration.requestExtension);
    XCTAssertEqual(parsed.replyPattern, arbitration.replyPattern);
    XCTAssertEqual(parsed.replyMask, arbitration.replyMask);
    XCTAssertEqual(parsed.replyExtension, arbitration.replyExtension);
}

- (void)testStartPeriodicMessageDataAndTiming {
    CANyonero::Arbitration arbitration;
    arbitration.request = 0x6F1;
    arbitration.requestExtension = 0x00;
    arbitration.replyPattern = 0x612;
    arbitration.replyMask = 0x7FF;
    arbitration.replyExtension = 0x00;
    CANyonero::Bytes payload = { 0x22, 0xF1, 0x90 };

    auto pdu = CANyonero::PDU::startPeriodicMessage(4, arbitration, payload);
    XCTAssertEqual(pdu.type(), CANyonero::PDUType::startPeriodicMessage);
    XCTAssertEqual(pdu.milliseconds(), 2000);

    auto parsedArb = pdu.arbitration();
    XCTAssertEqual(parsedArb.request, arbitration.request);
    XCTAssertEqual(parsedArb.replyPattern, arbitration.replyPattern);

    auto extractedData = pdu.data();
    XCTAssertEqual(extractedData, payload);
}

- (void)testInfoPayloadParsing {
    auto infoPdu = CANyonero::PDU::info("Vendor", "Model", "HW", "SN123", "FW1.0");
    auto info = infoPdu.information();
    XCTAssertEqual(info.vendor, "Vendor");
    XCTAssertEqual(info.model, "Model");
    XCTAssertEqual(info.hardware, "HW");
    XCTAssertEqual(info.serial, "SN123");
    XCTAssertEqual(info.firmware, "FW1.0");
}

- (void)testRpcSendBinaryFilename {
    auto pdu = CANyonero::PDU::rpcSendBinary("update.bin");
    XCTAssertEqual(pdu.type(), CANyonero::PDUType::rpcSendBinary);
    XCTAssertEqual(pdu.filename(), "update.bin");
}

- (void)testContainsPDUWithLeadingGarbage {
    auto pong = CANyonero::PDU::pong({ 0xAA });
    auto frame = pong.frame();
    CANyonero::Bytes withGarbage = { 0x00, 0xFF };
    withGarbage.insert(withGarbage.end(), frame.begin(), frame.end());

    XCTAssertEqual(CANyonero::PDU::containsPDU(withGarbage), -2);
}

@end
