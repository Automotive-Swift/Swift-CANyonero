///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#ifndef ISOTP_HPP
#define ISOTP_HPP
#pragma once

#include "Helpers.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace CANyonero {

namespace ISOTP {

const size_t maximumTransferSize = 0xFFF; // 4095 bytes
const size_t maximumUnconfirmedBlocks = 1000; // 586 + n ;-)
const uint8_t padding = 0xAA;

struct Frame {

    /// The Frame Type.
    enum class Type: uint8_t {
        single      = 0x00,
        first       = 0x10,
        consecutive = 0x20,
        flowControl = 0x30,
        invalid     = 0xFF
    };

    /// The Flow Control Status.
    enum class FlowStatus: uint8_t {
        clearToSend = 0x00,
        wait        = 0x01,
        overflow    = 0x02,
        invalid     = 0x0F,
    };

    Bytes bytes;

    /// Creates a frame from its on-the-wire structure (exactly 7 or 8 bytes).
    Frame(const Bytes& bytes)
    :bytes(bytes)
    {
    }

    /// Returns an FC.
    static Frame flowControl(FlowStatus status, uint8_t blockSize, uint8_t separationTime, uint8_t width) {
        uint8_t pci = uint8_t(Type::flowControl) | uint8_t(status);
        std::vector<uint8_t> vector { pci, blockSize, separationTime };
        vector.resize(width, ISOTP::padding);
        return Frame(vector);
    }

    /// Returns an SF.
    static Frame single(const Bytes& bytes, uint8_t width) {
        assert(bytes.size() <= 7);
        uint8_t pci = uint8_t(Type::single) | uint8_t(bytes.size());
        auto vector = std::vector<uint8_t> { pci };
        vector.insert(vector.end(), bytes.begin(), bytes.end());
        vector.resize(width, ISOTP::padding);
        return Frame(vector);
    }

    /// Returns a FF.
    static Frame first(uint16_t pduLength, const Bytes& bytes, uint8_t width) {
        uint8_t pciHi = uint8_t(Type::first) | uint8_t(pduLength >> 8);
        uint8_t pciLo = uint8_t(pduLength & 0xFF);
        auto vector = std::vector<uint8_t> { pciHi, pciLo };
        vector.insert(vector.end(), bytes.begin(), bytes.begin() + width - 2);
        return Frame(vector);
    }

    /// Returns a CF.
    static Frame consecutive(uint8_t sequenceNumber, const Bytes& bytes, uint8_t count, uint8_t width) {
        assert(sequenceNumber <= 0x0F);
        assert(count);
        assert(count <= width);
        uint8_t pci = uint8_t(Type::consecutive) | uint8_t(sequenceNumber);
        auto vector = std::vector<uint8_t> { pci };
        vector.insert(vector.end(), bytes.begin(), bytes.begin() + count);
        vector.resize(width, ISOTP::padding);
        return Frame(vector);
    }

    /// Returns the frame type.
    Type type() {
        switch (bytes[0] & 0xF0) {
            case 0x00: return Type::single;
            case 0x10: return Type::first;
            case 0x20: return Type::consecutive;
            case 0x30: return Type::flowControl;
            default:   return Type::invalid;
        }
    }

    /// Returns the FC status.
    FlowStatus flowStatus() {
        assert((bytes[0] & 0xF0) == 0x30); // ensure this is a flow control frame
        switch(bytes[0] & 0x0F) {
            case 0x00: return FlowStatus::clearToSend;
            case 0x01: return FlowStatus::wait;
            case 0x02: return FlowStatus::overflow;
            default:   return FlowStatus::invalid;
        }
    }

    /// Returns the PDU length for an SF.
    uint8_t singleLength() {
        assert((bytes[0] & 0xF0) == 0x00); // ensure this is a single frame
        return bytes[0] & 0x0F;
    }

    /// Returns the PDU length for an FF.
    uint16_t firstLength() {
        assert((bytes[0] & 0xF0) == 0x10); // ensure this is a first frame
        return uint16_t((bytes[0] & 0x0F)) << 8 | bytes[1];
    }

    /// Returns SN.
    uint8_t consecutiveSequenceNumber() {
        assert((bytes[0] & 0xF0) == 0x20); // ensure this is a consecutive frame
        return bytes[0] & 0x0F;
    }

    /// Returns BS.
    uint8_t blockSize() {
        assert((bytes[0] & 0xF0) == 0x30); // ensure this is a flow control frame
        return bytes[1];
    }

    /// Returns ST in microseconds.
    uint16_t separationTime() {
        assert((bytes[0] & 0xF0) == 0x30); // ensure this is a flow control frame
        return separationTimeToMicroseconds(bytes[2]);
    }
    
    static uint16_t separationTimeToMicroseconds(SeparationTime stMin) {
        // Conversion as defined by ISO-15765-2:2016:
        // 1. Up to (and including) 0x7F, it's milliseconds.
        if (stMin < 0x80) { return stMin * 1000; }
        // 2. 0x80 - 0xF0 are reserved and therefore we can choose a default (0ms).
        if (stMin < 0xF1) { return 0; }
        // 3. Between 0xF1 and 0xF9, it's microseconds * 100.
        if (stMin < 0xFA) { return 100 * (stMin - 0xF0); }
        // 4. 0xFA - 0xFF are reserved and therefore we can choose a default (0ms).
        return 0;
    }

    static SeparationTime microsecondsToSeparationTime(uint16_t microseconds) {
        // Simplified conversion as defined by ISO-15765-2:2016. We only
        // support inter frame times up to 10 milliseconds. Moreover, since we
        // can't achieve true microsecond granularity, we round.
        if (microseconds < 50) { return 0; }
        if (microseconds < 150) { return 0xF1; }
        if (microseconds < 250) { return 0xF2; }
        if (microseconds < 350) { return 0xF3; }
        if (microseconds < 450) { return 0xF4; }
        if (microseconds < 550) { return 0xF5; }
        if (microseconds < 650) { return 0xF6; }
        if (microseconds < 750) { return 0xF7; }
        if (microseconds < 850) { return 0xF8; }
        if (microseconds < 950) { return 0xF9; }
        if (microseconds < 1500) { return 1; }
        if (microseconds < 2500) { return 2; }
        if (microseconds < 3500) { return 3; }
        if (microseconds < 4500) { return 4; }
        if (microseconds < 5500) { return 5; }
        if (microseconds < 6500) { return 6; }
        if (microseconds < 7500) { return 7; }
        if (microseconds < 8500) { return 8; }
        if (microseconds < 9500) { return 9; }
        return 10;
    }
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

/// An implementation of an ISO 15765-2 (ISOTP) protocol machine with an emphasis
/// on a simple, but comprehensive and well testable API.
///
/// This implementation works with CAN 2.0 standard and extended addressing modes.
/// NOTE: The implementation does not enforce any timings, so you
/// should be safe to use this with whatever (slow) ECU you're talking to.
class Transceiver {
public:
    enum class Behavior {
        defensive,
        strict,
    };

    enum class Mode {
        standard,
        extended,
    };

    enum class State {
        idle,
        sending,
        receiving,
    };

    struct Action {

        enum class Type {
            process,
            writeFrames,
            waitForMore,
            protocolViolation,
        };

        Type type;
        std::string error;
        Bytes data;
        std::vector<Frame> frames;
        uint16_t separationTime;
    };

    Behavior behavior;
    uint8_t width;
    uint8_t blockSize;
    uint16_t rxSeparationTime;
    uint16_t txSeparationTime;

    // State
    State state = State::idle;
    Bytes sendingPayload;
    uint8_t sendingSequenceNumber = 0;

    Bytes receivingPayload;
    uint8_t receivingSequenceNumber = 0;
    uint16_t receivingPendingCounter = 0;
    uint16_t receivingUnconfirmedFramesCounter = 0;

    /// Create a ``Transceiver`` with a default configuration.
    Transceiver()
    :behavior(Behavior::defensive), width(8), blockSize(0), rxSeparationTime(0), txSeparationTime(0)
    {
    }

    /// Create a new ``Transceiver`` with a custom configuration.
    /// The rxSeparationTime and txSeparationTime are in microseconds.
    /// NOTE: txSeparationTime is only considered, if it is larger than the one reported in the respective flow.
    Transceiver(Behavior behavior, Mode mode, uint8_t blockSize = 0x00, uint16_t rxSeparationTime = 0x00, uint16_t txSeparationTime = 0x00)
    :behavior(behavior), width(mode == Mode::standard ? 8 : 7), blockSize(blockSize), rxSeparationTime(rxSeparationTime), txSeparationTime(txSeparationTime)
    {
    }

    /// Send a PDU. Short (`size < width`) PDUs are passed through, longer PDUs launch the state machine.
    Action writePDU(const Bytes& bytes) {
        if (bytes.size() > ISOTP::maximumTransferSize) { return { Action::Type::protocolViolation, "Exceeding maximum ISOTP transfer size." }; }

        if (state != State::idle) { return { Action::Type::protocolViolation, "State machine not .idle" }; }

        if (bytes.size() < width) {
            // Content small enough to fit in a single frame, send single frame and leave state machine in `.idle`.
            auto frame = Frame::single(bytes, width);
            return { .type = Action::Type::writeFrames, .frames = { 1, frame } };
        }

        // Content large enough to require multiple frames. Send first frame and proceeding state machine to `.sending`.
        auto frame = Frame::first(bytes.size(), bytes, width);
        state = State::sending;
        sendingPayload = Bytes(bytes.begin() + width - 2, bytes.end());
        sendingSequenceNumber = 0x01;
        return { .type = Action::Type::writeFrames, .frames = { 1, frame } };
    }

    /// Call this for any incoming frame.
    Action didReceiveFrame(const Bytes& bytes) {
        if (bytes.size() != width) {
            // Allow unpadded flow control which some ECUs are using (BMW 8HP TCU, I'm looking at you!)
            if (!(bytes.size() == 3 && bytes[0] >= 0x30 && bytes[0] <= 0x32)) {
                return { Action::Type::protocolViolation, "Incoming frame does not match predefined width." };
            }
        }

        switch (behavior) {
            case Behavior::strict: {
                switch (state) {
                    case State::sending: {
                        return parseFlowControlFrame(bytes);
                    default:
                        return parseDataFrame(bytes);
                    }
                }
            }
            case Behavior::defensive: {
                Action action;
                switch (state) {
                    case State::sending: {
                        action = parseFlowControlFrame(bytes);
                        break;
                        
                    default:
                        action = parseDataFrame(bytes);
                    }
                }
                if (action.type == Action::Type::protocolViolation) {
                    // reset state machine and try again assuming it's a data frame.
                    reset();
                    action = parseDataFrame(bytes);
                    if (action.type == Action::Type::protocolViolation) {
                        // still an error, reset again (effectively ignoring the frame).
                        return { Action::Type::waitForMore };
                    } else {
                        return action;
                    }
                }
                return action;
            }
        }
        assert(false);
    }

private:
    Action parseFlowControlFrame(const Bytes& bytes) {
        auto frame = Frame(bytes);
        if (frame.type() != Frame::Type::flowControl) { return { Action::Type::protocolViolation, "Unexpected frame type received while sending. Did expect FLOW CONTROL." }; }
        
        switch (frame.flowStatus()) {

            case Frame::FlowStatus::clearToSend: {
                uint16_t numberOfUnconfirmedFrames = frame.blockSize();
                if (numberOfUnconfirmedFrames == 0) {
                    numberOfUnconfirmedFrames = ISOTP::maximumUnconfirmedBlocks;
                }
                auto nextFrames = std::vector<Frame> {};
                for (uint16_t i = 0; i < numberOfUnconfirmedFrames; ++i) {
                    auto nextChunkSize = std::min(width - 1, static_cast<int>(sendingPayload.size()));
                    auto nextFrame = Frame::consecutive(sendingSequenceNumber, sendingPayload, nextChunkSize, width);
                    sendingPayload.erase(sendingPayload.begin(), sendingPayload.begin() + nextChunkSize);
                    nextFrames.insert(nextFrames.end(), nextFrame);
                    
                    if (sendingPayload.empty()) {
                        reset();
                        break;
                    }
                    sendingSequenceNumber = (sendingSequenceNumber + 1) & 0x0F;
                }
                return {
                    .type = Action::Type::writeFrames,
                    .frames = nextFrames,
                    // NOTE: We are taking the maximum separation time from the received flow control frame
                    // and the separation time configured for this transceiver.
                    .separationTime = std::max(frame.separationTime(), txSeparationTime),
                };
            }
                
            case Frame::FlowStatus::wait:
                return { Action::Type::waitForMore };

            case Frame::FlowStatus::overflow:
                return { Action::Type::protocolViolation, "Received FLOW CONTROL w/ status OVERFLOW." };

            default:
                return { Action::Type::protocolViolation, "Received FLOW CONTROL w/ invalid status." };
        }
    }

    Action parseDataFrame(const Bytes& bytes) {
        auto frame = Frame(bytes);
        switch (frame.type()) {
            case Frame::Type::single: {
                if (state != State::idle) { return { Action::Type::protocolViolation, "Did receive SINGLE while we're not idle." }; }

                auto pduLength = frame.singleLength();
                if (pduLength == 0)  { return { Action::Type::protocolViolation, "Did receive SINGLE with zero length in PCI." }; }
                if (pduLength > bytes.size() - 1) { return { Action::Type::protocolViolation, "Did receive SINGLE with length exceeding payload." }; }
                if (pduLength > 7) { return { Action::Type::protocolViolation, "Did receive SINGLE with invalid length > 7." }; }
                auto data = Bytes(bytes.begin() + 1, bytes.begin() + 1 + pduLength);
                // No need to call reset() as we didn't change anything in the state machine.
                return {
                    .type = Action::Type::process,
                    .data = data,
                };
            }

            case Frame::Type::first: {
                if (state != State::idle) { return { Action::Type::protocolViolation, "Did receive FIRST while we're not idle." }; }

                auto pduLength = frame.firstLength();
                if (pduLength < 8) { return { Action::Type::protocolViolation, "Did receive FIRST with invalid length < 8." }; }
                receivingPayload = std::vector<uint8_t>(bytes.begin() + 2, bytes.end());
                receivingPendingCounter = pduLength - (width - 2);
                receivingUnconfirmedFramesCounter = blockSize;
                if (receivingUnconfirmedFramesCounter == 0) {
                    receivingUnconfirmedFramesCounter = ISOTP::maximumUnconfirmedBlocks;
                }
                state = State::receiving;
                receivingSequenceNumber = 0x01;
                auto frame = Frame::flowControl(Frame::FlowStatus::clearToSend, blockSize, rxSeparationTime, width);
                return {
                    .type = Action::Type::writeFrames,
                    .frames = { 1, frame }
                };
            }

            case Frame::Type::consecutive: {
                if (state != State::receiving) { return { Action::Type::protocolViolation, "Did receive CONSECUTIVE while we're not receiving." }; }

                if (frame.consecutiveSequenceNumber() != receivingSequenceNumber) { return { Action::Type::protocolViolation, "Did receive CONSECUTIVE with unexpected sequence number." }; }
                receivingSequenceNumber = (receivingSequenceNumber + 1) & 0x0F;

                auto length = std::min<uint16_t>(width - 1, receivingPendingCounter);
                receivingPayload.insert(receivingPayload.end(), bytes.begin() + 1, bytes.begin() + 1 + length);
                receivingPendingCounter -= length;
                if (receivingPendingCounter == 0) {
                    auto action = Action {
                        .type = Action::Type::process,
                        .data = receivingPayload
                    };
                    reset();
                    return action;
                }

                receivingUnconfirmedFramesCounter--;
                if (receivingUnconfirmedFramesCounter > 0) { return { Action::Type::waitForMore }; }

                receivingUnconfirmedFramesCounter = blockSize;
                auto frame = Frame::flowControl(Frame::FlowStatus::clearToSend, blockSize, rxSeparationTime, width);
                return {
                    .type = Action::Type::writeFrames,
                    .frames = { 1, frame }
                };
            }

            default:
                return { Action::Type::protocolViolation, "Unexpected frame type received. Did expect SINGLE, FIRST, or CONSECUTIVE." };

        }
    }

    void reset() {
        state = State::idle;
        sendingPayload.clear();
        sendingSequenceNumber = 0;
        receivingPayload.clear();
        receivingSequenceNumber = 0;
        receivingPendingCounter = 0;
        receivingUnconfirmedFramesCounter = 0;
    }
};
#pragma GCC diagnostic push

}
}
#endif
