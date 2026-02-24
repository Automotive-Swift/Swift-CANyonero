///
/// CANyonero. (C) 2022 - 2026 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#ifndef ISOTPFD_HPP
#define ISOTPFD_HPP
#pragma once

#include "ISOTP.hpp"

#include <limits>

namespace CANyonero {

namespace ISOTP {

constexpr uint8_t maximumFDStandardFrameWidth = 64;
constexpr uint8_t maximumFDExtendedFrameWidth = 63;

constexpr bool isValidCANFDLength(uint8_t length) {
    switch (length) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 12:
        case 16:
        case 20:
        case 24:
        case 32:
        case 48:
        case 64:
            return true;
        default:
            return false;
    }
}

constexpr uint8_t nextValidCANFDLength(uint8_t length) {
    if (length <= 8) { return length; }
    if (length <= 12) { return 12; }
    if (length <= 16) { return 16; }
    if (length <= 20) { return 20; }
    if (length <= 24) { return 24; }
    if (length <= 32) { return 32; }
    if (length <= 48) { return 48; }
    return 64;
}

constexpr uint8_t defaultFDMaximumFrameWidth(bool extendedMode) {
    return extendedMode ? maximumFDExtendedFrameWidth : maximumFDStandardFrameWidth;
}

constexpr bool isValidFDFrameWidth(uint8_t width, bool extendedMode) {
    if (extendedMode) {
        if (width > maximumFDExtendedFrameWidth) { return false; }
        return isValidCANFDLength(static_cast<uint8_t>(width + 1));
    }
    if (width > maximumFDStandardFrameWidth) { return false; }
    return isValidCANFDLength(width);
}

constexpr uint8_t nextValidFDFrameWidth(uint8_t requiredWidth, bool extendedMode) {
    if (extendedMode) {
        auto physicalLength = nextValidCANFDLength(static_cast<uint8_t>(requiredWidth + 1));
        return static_cast<uint8_t>(physicalLength - 1);
    }
    return nextValidCANFDLength(requiredWidth);
}

constexpr uint8_t singleFramePayloadCapacityFD(uint8_t width) {
    return width > standardFrameWidth ? static_cast<uint8_t>(width - 2) : static_cast<uint8_t>(width - 1);
}

/// A dedicated ISO-TP transceiver for CAN-FD channels.
/// This class enforces CAN-FD DLC validity and transmits frames using
/// the shortest valid DLC that can hold the current payload chunk.
class TransceiverFD {
public:
    using Behavior = Transceiver::Behavior;
    using Mode = Transceiver::Mode;
    using State = Transceiver::State;
    using Action = Transceiver::Action;

    TransceiverFD()
        :behavior(Behavior::defensive), mode(Mode::standard), maxFrameWidth(defaultFDMaximumFrameWidth(false)), blockSize(0), rxSeparationTime(0), txSeparationTime(0)
    {
    }

    /// Create a new CAN-FD transceiver.
    /// The optional ``frameWidth`` parameter defines the maximum effective frame width.
    /// For standard addressing this is up to 64, for extended addressing up to 63.
    TransceiverFD(Behavior behavior, Mode mode, uint8_t blockSize = 0x00, uint16_t rxSeparationTime = 0x00, uint16_t txSeparationTime = 0x00, uint8_t frameWidth = 0x00)
        :behavior(behavior), mode(mode), maxFrameWidth(resolveMaximumFrameWidth(mode, frameWidth)), blockSize(blockSize), rxSeparationTime(rxSeparationTime), txSeparationTime(txSeparationTime)
    {
    }

    /// Send a PDU. Single-frame payloads are encoded with dynamic DLC.
    Action writePDU(Bytes bytes) {
        if (bytes.size() > ISOTP::maximumTransferSize) { return { Action::Type::protocolViolation, "Exceeding maximum ISOTP transfer size." }; }
        if (state != State::idle) { return { Action::Type::protocolViolation, "State machine not .idle" }; }

        auto maxSinglePayload = static_cast<size_t>(singleFramePayloadCapacityFD(maxFrameWidth));
        if (bytes.size() <= maxSinglePayload) {
            auto frame = singleFrame(bytes);
            return { .type = Action::Type::writeFrames, .frames = { 1, frame } };
        }

        auto firstPayload = std::min<size_t>(static_cast<size_t>(maxFrameWidth - 2), bytes.size());
        auto frame = firstFrame(static_cast<uint16_t>(bytes.size()), bytes, firstPayload);
        state = State::sending;
        sendingPayload = std::move(bytes);
        sendingPayloadOffset = firstPayload;
        sendingSequenceNumber = 0x01;
        return { .type = Action::Type::writeFrames, .frames = { 1, frame } };
    }

    /// Call this for any incoming frame.
    Action didReceiveFrame(const Bytes& bytes) {
        if (bytes.empty()) { return { Action::Type::protocolViolation, "Incoming frame is empty." }; }
        if (bytes.size() > maxFrameWidth) { return { Action::Type::protocolViolation, "Incoming frame exceeds configured CAN-FD width." }; }
        if (!isValidFDFrameWidth(static_cast<uint8_t>(bytes.size()), mode == Mode::extended)) {
            return { Action::Type::protocolViolation, "Incoming frame uses invalid CAN-FD DLC length." };
        }

        switch (behavior) {
            case Behavior::strict: {
                switch (state) {
                    case State::sending: return parseFlowControlFrame(bytes);
                    default: return parseDataFrame(bytes);
                }
            }
            case Behavior::defensive: {
                Action action;
                switch (state) {
                    case State::sending: {
                        action = parseFlowControlFrame(bytes);
                        break;
                    }
                    default:
                        action = parseDataFrame(bytes);
                }
                if (action.type == Action::Type::protocolViolation) {
                    reset();
                    action = parseDataFrame(bytes);
                    if (action.type == Action::Type::protocolViolation) {
                        return { Action::Type::waitForMore };
                    }
                    return action;
                }
                return action;
            }
        }
        assert(false);
    }

    State machineState() const { return state; }

    void reset() {
        state = State::idle;
        sendingPayload.clear();
        sendingPayloadOffset = 0;
        sendingSequenceNumber = 0;
        receivingPayload.clear();
        receivingSequenceNumber = 0;
        receivingPendingCounter = 0;
        receivingUnconfirmedFramesCounter = 0;
    }

private:
    static uint8_t resolveMaximumFrameWidth(Mode mode, uint8_t requestedWidth) {
        const bool extendedMode = mode == Mode::extended;
        const auto minimumWidth = extendedMode ? extendedFrameWidth : standardFrameWidth;
        const auto maximumWidth = defaultFDMaximumFrameWidth(extendedMode);

        if (requestedWidth == 0) { return maximumWidth; }

        auto clamped = std::max(minimumWidth, std::min(requestedWidth, maximumWidth));
        if (!isValidFDFrameWidth(clamped, extendedMode)) {
            clamped = nextValidFDFrameWidth(clamped, extendedMode);
        }
        return std::min(clamped, maximumWidth);
    }

    uint8_t dynamicFrameWidthFor(uint8_t requiredBytes) const {
        auto width = nextValidFDFrameWidth(requiredBytes, mode == Mode::extended);
        return std::min(width, maxFrameWidth);
    }

    Frame flowControlFrame() const {
        auto width = dynamicFrameWidthFor(3);
        return Frame::flowControl(Frame::FlowStatus::clearToSend, blockSize, rxSeparationTime, width);
    }

    Frame singleFrame(const Bytes& bytes) const {
        auto payloadSize = static_cast<uint8_t>(bytes.size());
        std::vector<uint8_t> vector;
        uint8_t width = 0;
        if (payloadSize <= 0x07) {
            uint8_t pci = static_cast<uint8_t>(static_cast<uint8_t>(Frame::Type::single) | payloadSize);
            vector = { pci };
            width = dynamicFrameWidthFor(static_cast<uint8_t>(payloadSize + 1));
        } else {
            vector = { static_cast<uint8_t>(Frame::Type::single), payloadSize };
            width = dynamicFrameWidthFor(static_cast<uint8_t>(payloadSize + 2));
        }
        vector.insert(vector.end(), bytes.begin(), bytes.end());
        vector.resize(width, ISOTP::padding);
        return Frame(vector);
    }

    Frame firstFrame(uint16_t pduLength, const Bytes& bytes, size_t payloadCount) const {
        uint8_t pciHi = static_cast<uint8_t>(static_cast<uint8_t>(Frame::Type::first) | static_cast<uint8_t>(pduLength >> 8));
        uint8_t pciLo = static_cast<uint8_t>(pduLength & 0xFF);
        auto vector = std::vector<uint8_t> { pciHi, pciLo };
        vector.insert(vector.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(payloadCount));
        auto width = dynamicFrameWidthFor(static_cast<uint8_t>(payloadCount + 2));
        vector.resize(width, ISOTP::padding);
        return Frame(vector);
    }

    Frame consecutiveFrame(uint8_t sequenceNumber, const Bytes& bytes, size_t offset, uint8_t count) const {
        uint8_t pci = static_cast<uint8_t>(static_cast<uint8_t>(Frame::Type::consecutive) | static_cast<uint8_t>(sequenceNumber & 0x0F));
        auto vector = std::vector<uint8_t> { pci };
        vector.insert(vector.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
        auto width = dynamicFrameWidthFor(static_cast<uint8_t>(count + 1));
        vector.resize(width, ISOTP::padding);
        return Frame(vector);
    }

    Behavior behavior;
    Mode mode;
    uint8_t maxFrameWidth;
    uint8_t blockSize;
    uint16_t rxSeparationTime;
    uint16_t txSeparationTime;

    State state = State::idle;
    Bytes sendingPayload;
    size_t sendingPayloadOffset = 0;
    uint8_t sendingSequenceNumber = 0;

    Bytes receivingPayload;
    uint8_t receivingSequenceNumber = 0;
    uint16_t receivingPendingCounter = 0;
    uint16_t receivingUnconfirmedFramesCounter = 0;

private:
    Action parseFlowControlFrame(const Bytes& bytes) {
        if (bytes.size() < 3) {
            return { Action::Type::protocolViolation, "Received FLOW CONTROL shorter than minimum length 3." };
        }

        auto frame = Frame(bytes);
        if (frame.type() != Frame::Type::flowControl) {
            return { Action::Type::protocolViolation, "Unexpected frame type received while sending. Did expect FLOW CONTROL." };
        }

        switch (frame.flowStatus()) {
            case Frame::FlowStatus::clearToSend: {
                uint16_t numberOfUnconfirmedFrames = frame.blockSize();
                if (numberOfUnconfirmedFrames == 0) {
                    numberOfUnconfirmedFrames = std::numeric_limits<uint16_t>::max();
                }

                if (sendingPayloadOffset > sendingPayload.size()) {
                    reset();
                    return { Action::Type::protocolViolation, "Sending payload offset exceeds payload size." };
                }

                auto nextFrames = std::deque<Frame> {};
                for (uint16_t i = 0; i < numberOfUnconfirmedFrames; ++i) {
                    auto remaining = sendingPayload.size() - sendingPayloadOffset;
                    if (remaining == 0) {
                        reset();
                        break;
                    }

                    auto nextChunkSize = static_cast<uint8_t>(std::min<size_t>(maxFrameWidth - 1, remaining));
                    auto nextFrame = consecutiveFrame(sendingSequenceNumber, sendingPayload, sendingPayloadOffset, nextChunkSize);
                    sendingPayloadOffset += nextChunkSize;
                    nextFrames.insert(nextFrames.end(), nextFrame);

                    if (sendingPayloadOffset >= sendingPayload.size()) {
                        reset();
                        break;
                    }

                    sendingSequenceNumber = static_cast<uint8_t>((sendingSequenceNumber + 1) & 0x0F);
                }

                return {
                    .type = Action::Type::writeFrames,
                    .frames = nextFrames,
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

                uint8_t headerSize = 1;
                auto pduLength = static_cast<uint16_t>(frame.singleLength(standardFrameWidth));
                if (bytes.size() > standardFrameWidth) {
                    if ((bytes[0] & 0x0F) != 0 || bytes.size() < 2) {
                        return { Action::Type::protocolViolation, "Did receive SINGLE with invalid CAN-FD single-frame PCI." };
                    }
                    headerSize = 2;
                    pduLength = bytes[1];
                }

                if (pduLength == 0) { return { Action::Type::protocolViolation, "Did receive SINGLE with zero length in PCI." }; }
                if (pduLength > bytes.size() - headerSize) { return { Action::Type::protocolViolation, "Did receive SINGLE with length exceeding payload." }; }
                if (pduLength > singleFramePayloadCapacityFD(static_cast<uint8_t>(bytes.size()))) {
                    return { Action::Type::protocolViolation, "Did receive SINGLE with invalid length for current frame width." };
                }

                auto data = Bytes(bytes.begin() + headerSize, bytes.begin() + headerSize + pduLength);
                return {
                    .type = Action::Type::process,
                    .data = data,
                };
            }

            case Frame::Type::first: {
                if (state != State::idle) { return { Action::Type::protocolViolation, "Did receive FIRST while we're not idle." }; }
                if (bytes.size() < 3) { return { Action::Type::protocolViolation, "Did receive FIRST shorter than minimum length 3." }; }

                auto pduLength = frame.firstLength();
                auto firstPayloadLength = static_cast<uint16_t>(bytes.size() - 2);
                if (pduLength <= firstPayloadLength) {
                    return { Action::Type::protocolViolation, "Did receive FIRST with invalid length <= first-frame payload." };
                }

                receivingPayload = std::vector<uint8_t>(bytes.begin() + 2, bytes.end());
                receivingPendingCounter = pduLength - firstPayloadLength;
                receivingUnconfirmedFramesCounter = blockSize == 0 ? std::numeric_limits<uint16_t>::max() : blockSize;
                state = State::receiving;
                receivingSequenceNumber = 0x01;
                auto fcFrame = flowControlFrame();
                return {
                    .type = Action::Type::writeFrames,
                    .frames = { 1, fcFrame }
                };
            }

            case Frame::Type::consecutive: {
                if (state != State::receiving) { return { Action::Type::protocolViolation, "Did receive CONSECUTIVE while we're not receiving." }; }
                if (bytes.size() < 2) { return { Action::Type::protocolViolation, "Did receive CONSECUTIVE shorter than minimum length 2." }; }
                if (frame.consecutiveSequenceNumber() != receivingSequenceNumber) {
                    return { Action::Type::protocolViolation, "Did receive CONSECUTIVE with unexpected sequence number." };
                }

                receivingSequenceNumber = static_cast<uint8_t>((receivingSequenceNumber + 1) & 0x0F);
                auto length = static_cast<uint16_t>(std::min<size_t>(bytes.size() - 1, receivingPendingCounter));
                receivingPayload.insert(receivingPayload.end(), bytes.begin() + 1, bytes.begin() + 1 + length);
                receivingPendingCounter = static_cast<uint16_t>(receivingPendingCounter - length);

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

                receivingUnconfirmedFramesCounter = blockSize == 0 ? std::numeric_limits<uint16_t>::max() : blockSize;
                auto fcFrame = flowControlFrame();
                return {
                    .type = Action::Type::writeFrames,
                    .frames = { 1, fcFrame }
                };
            }

            default:
                return { Action::Type::protocolViolation, "Unexpected frame type received. Did expect SINGLE, FIRST, or CONSECUTIVE." };
        }
    }
};

}
}

#endif
