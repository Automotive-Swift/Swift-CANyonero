///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#ifndef TP20_HPP
#define TP20_HPP
#pragma once

#include "Helpers.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace CANyonero {

namespace TP20 {

constexpr size_t maximumTransferSize = 0xFFFF;
constexpr uint8_t frameWidth = 8;
constexpr uint8_t firstFrameDataCapacity = 5;
constexpr uint8_t consecutiveFrameDataCapacity = 7;
constexpr uint16_t defaultWaitTimeMs = 100;
constexpr uint8_t maximumRetransmitRequests = 5;

struct ConnectionParameters {
    uint8_t blockSize;
    uint8_t t1;
    uint8_t t2;
    uint8_t t3;
    uint8_t t4;

    static ConnectionParameters activeDefaults() {
        return { 0x0F, 0xC4, 0xFF, 0x01, 0xFF };
    }

    static ConnectionParameters passiveDefaults() {
        return { 0x0F, 0x8A, 0xFF, 0x0A, 0xFF };
    }

    uint8_t effectiveBlockSize(const ConnectionParameters& remote) const {
        auto localBlockSize = blockSize == 0 ? uint8_t(1) : blockSize;
        auto remoteBlockSize = remote.blockSize == 0 ? uint8_t(1) : remote.blockSize;
        return std::max<uint8_t>(1, std::min(localBlockSize, remoteBlockSize));
    }
};

inline uint32_t timingByteToMicroseconds(uint8_t timingByte) {
    if (timingByte == 0xFF) {
        return std::numeric_limits<uint32_t>::max();
    }
    static constexpr uint32_t bases[] = { 100, 1000, 10000, 100000 };
    auto base = bases[(timingByte >> 6) & 0x03];
    auto value = timingByte & 0x3F;
    return base * value;
}

inline uint8_t microsecondsToTimingByte(uint32_t microseconds) {
    if (microseconds == std::numeric_limits<uint32_t>::max()) {
        return 0xFF;
    }

    struct Candidate {
        uint32_t base;
        uint8_t selector;
    };

    static constexpr Candidate candidates[] = {
        { 100, 0x00 },
        { 1000, 0x40 },
        { 10000, 0x80 },
        { 100000, 0xC0 },
    };

    for (auto candidate : candidates) {
        auto value = microseconds / candidate.base;
        if (value > 0 && value < 0x40 && value * candidate.base == microseconds) {
            return static_cast<uint8_t>(candidate.selector | value);
        }
    }

    if (microseconds < 1000) {
        return 0x01;
    }
    if (microseconds < 10000) {
        return static_cast<uint8_t>(0x40 | std::min<uint32_t>(0x3F, microseconds / 1000));
    }
    if (microseconds < 100000) {
        return static_cast<uint8_t>(0x80 | std::min<uint32_t>(0x3F, microseconds / 10000));
    }
    return static_cast<uint8_t>(0xC0 | std::min<uint32_t>(0x3F, microseconds / 100000));
}

struct Frame {
    enum class Type : uint8_t {
        data,
        acknowledge,
        connectionSetup,
        connectionAcknowledge,
        connectionTest,
        breakMessage,
        disconnect,
        invalid,
    };

    Bytes bytes;

    Frame() = default;
    explicit Frame(Bytes bytes)
        : bytes(std::move(bytes)) {
    }

    static Frame data(uint8_t sequenceNumber, bool acknowledgeRequested, bool endOfMessage, const Bytes& payload) {
        uint8_t control = static_cast<uint8_t>(sequenceNumber & 0x0F);
        if (!acknowledgeRequested) {
            control |= 0x20;
        }
        if (endOfMessage) {
            control |= 0x10;
        }

        auto frameBytes = Bytes(1, control);
        frameBytes.insert(frameBytes.end(), payload.begin(), payload.end());
        return Frame(frameBytes);
    }

    static Frame acknowledge(uint8_t nextSequenceNumber, bool receiverReady = true) {
        uint8_t control = 0x90 | static_cast<uint8_t>(nextSequenceNumber & 0x0F);
        if (receiverReady) {
            control |= 0x20;
        }
        return Frame(Bytes(1, control));
    }

    static Frame connectionSetup(const ConnectionParameters& parameters) {
        return Frame(Bytes { 0xA0, static_cast<uint8_t>(parameters.blockSize & 0x0F), parameters.t1, parameters.t2, parameters.t3, parameters.t4 });
    }

    static Frame connectionAcknowledge(const ConnectionParameters& parameters) {
        return Frame(Bytes { 0xA1, static_cast<uint8_t>(parameters.blockSize & 0x0F), parameters.t1, parameters.t2, parameters.t3, parameters.t4 });
    }

    static Frame connectionTest() {
        return Frame(Bytes { 0xA3 });
    }

    static Frame breakMessage() {
        return Frame(Bytes { 0xA4 });
    }

    static Frame disconnect() {
        return Frame(Bytes { 0xA8 });
    }

    Type type() const {
        if (bytes.empty()) {
            return Type::invalid;
        }

        auto control = bytes[0];
        if ((control & 0xC0) == 0x00) {
            return Type::data;
        }
        if ((control & 0xF0) == 0x90 || (control & 0xF0) == 0xB0) {
            return Type::acknowledge;
        }
        switch (control) {
            case 0xA0:
                return Type::connectionSetup;
            case 0xA1:
                return Type::connectionAcknowledge;
            case 0xA3:
                return Type::connectionTest;
            case 0xA4:
                return Type::breakMessage;
            case 0xA8:
                return Type::disconnect;
            default:
                return Type::invalid;
        }
    }

    bool acknowledgeRequested() const {
        return type() == Type::data && (bytes[0] & 0x20) == 0;
    }

    bool endOfMessage() const {
        return type() == Type::data && (bytes[0] & 0x10) != 0;
    }

    bool receiverReady() const {
        return type() == Type::acknowledge && (bytes[0] & 0x20) != 0;
    }

    uint8_t sequenceNumber() const {
        switch (type()) {
            case Type::data:
            case Type::acknowledge:
                return bytes[0] & 0x0F;
            default:
                return 0;
        }
    }

    Bytes payload() const {
        if (type() != Type::data || bytes.size() <= 1) {
            return {};
        }
        return Bytes(bytes.begin() + 1, bytes.end());
    }

    uint16_t declaredPayloadLength() const {
        auto dataBytes = payload();
        if (dataBytes.size() < 2) {
            return 0;
        }
        return static_cast<uint16_t>(dataBytes[0] << 8 | dataBytes[1]);
    }

    ConnectionParameters parameters() const {
        if ((type() != Type::connectionSetup && type() != Type::connectionAcknowledge) || bytes.size() < 6) {
            return ConnectionParameters::passiveDefaults();
        }
        return { static_cast<uint8_t>(bytes[1] & 0x0F), bytes[2], bytes[3], bytes[4], bytes[5] };
    }
};

class Transceiver {
public:
    enum class Behavior {
        defensive,
        strict,
    };

    enum class State {
        disconnected,
        awaitingConnectionAcknowledge,
        connected,
        sending,
        receiving,
    };

    enum class Event {
        none,
        connectionRequested,
        connected,
        connectionTest,
        disconnected,
        breakReceived,
    };

    struct Action {
        enum class Type {
            idle,
            writeFrames,
            process,
            processAndWriteFrames,
            waitForMore,
            event,
            protocolViolation,
        };

        Type type = Type::idle;
        std::string error;
        Bytes data;
        std::deque<Frame> frames;
        uint16_t separationTime = 0;
        uint16_t waitTimeMs = 0;
        Event event = Event::none;
    };

    explicit Transceiver(Behavior behavior = Behavior::defensive,
                         ConnectionParameters localParameters = ConnectionParameters::activeDefaults())
        : behavior(behavior),
          localParameters(localParameters),
          remoteParameters(ConnectionParameters::passiveDefaults()) {
    }

    void reset() {
        state = State::disconnected;
        remoteParameters = ConnectionParameters::passiveDefaults();
        resetSending();
        resetReceiving();
    }

    void setLocalParameters(ConnectionParameters parameters) {
        localParameters = parameters;
    }

    ConnectionParameters localConnectionParameters() const {
        return localParameters;
    }

    ConnectionParameters remoteConnectionParameters() const {
        return remoteParameters;
    }

    bool isConnected() const {
        return state == State::connected || state == State::sending || state == State::receiving;
    }

    State machineState() const {
        return state;
    }

    Action requestConnection() {
        resetSending();
        resetReceiving();
        state = State::awaitingConnectionAcknowledge;
        return framesAction({ Frame::connectionSetup(localParameters) }, 0, Event::connectionRequested);
    }

    Action connectionTest() const {
        if (!isConnected()) {
            return protocolViolation("Connection test requires an established TP2.0 session.");
        }
        return framesAction({ Frame::connectionTest() }, 0, Event::connectionTest);
    }

    Action disconnect() {
        if (!isConnected() && state != State::awaitingConnectionAcknowledge) {
            return idleAction();
        }

        auto action = framesAction({ Frame::disconnect() }, 0, Event::disconnected);
        reset();
        return action;
    }

    Action writePDU(Bytes bytes) {
        if (!isConnected()) {
            return protocolViolation("TP2.0 data transfer requires an established connection.");
        }
        if (state == State::sending && !lastTransmittedBlock.empty()) {
            return protocolViolation("A TP2.0 transfer is already in progress.");
        }
        if (bytes.empty()) {
            return protocolViolation("Cannot send an empty TP2.0 payload.");
        }
        if (bytes.size() > maximumTransferSize) {
            return protocolViolation("Exceeding maximum TP2.0 transfer size.");
        }

        sendingPayload = std::move(bytes);
        sendingOffset = 0;
        retransmitRequestCount = 0;
        lastTransmittedBlock.clear();
        state = State::sending;
        return emitNextBlock();
    }

    Action didReceiveFrame(const Bytes& bytes) {
        Frame frame(bytes);
        switch (frame.type()) {
            case Frame::Type::connectionSetup:
                return handleConnectionSetup(frame);
            case Frame::Type::connectionAcknowledge:
                return handleConnectionAcknowledge(frame);
            case Frame::Type::connectionTest:
                return handleConnectionTest();
            case Frame::Type::disconnect:
                return handleDisconnect();
            case Frame::Type::breakMessage:
                return eventAction(Event::breakReceived);
            case Frame::Type::acknowledge:
                return handleAcknowledge(frame);
            case Frame::Type::data:
                return handleData(frame);
            case Frame::Type::invalid:
            default:
                if (behavior == Behavior::defensive) {
                    return waitAction();
                }
                return protocolViolation("Unsupported TP2.0 frame.");
        }
    }

private:
    Behavior behavior;
    State state = State::disconnected;
    ConnectionParameters localParameters;
    ConnectionParameters remoteParameters;

    Bytes receivingPayload;
    size_t expectedReceiveLength = 0;
    uint8_t nextReceiveSequence = 0;

    Bytes sendingPayload;
    size_t sendingOffset = 0;
    uint8_t nextSendSequence = 0;
    uint8_t nextExpectedAckSequence = 0;
    uint8_t retransmitRequestCount = 0;
    std::deque<Frame> lastTransmittedBlock;

    static Action idleAction() {
        return Action {};
    }

    static Action waitAction(uint16_t waitTimeMs = 0, Event event = Event::none) {
        Action action;
        action.type = Action::Type::waitForMore;
        action.waitTimeMs = waitTimeMs;
        action.event = event;
        return action;
    }

    static Action eventAction(Event event) {
        Action action;
        action.type = Action::Type::event;
        action.event = event;
        return action;
    }

    static Action framesAction(std::deque<Frame> frames, uint16_t waitTimeMs = 0, Event event = Event::none, uint16_t separationTime = 0) {
        Action action;
        action.type = Action::Type::writeFrames;
        action.frames = std::move(frames);
        action.waitTimeMs = waitTimeMs;
        action.event = event;
        action.separationTime = separationTime;
        return action;
    }

    static Action processAction(Bytes data) {
        Action action;
        action.type = Action::Type::process;
        action.data = std::move(data);
        return action;
    }

    static Action processAndFramesAction(Bytes data, std::deque<Frame> frames, uint16_t waitTimeMs = 0, uint16_t separationTime = 0) {
        Action action;
        action.type = Action::Type::processAndWriteFrames;
        action.data = std::move(data);
        action.frames = std::move(frames);
        action.waitTimeMs = waitTimeMs;
        action.separationTime = separationTime;
        return action;
    }

    static Action protocolViolation(const std::string& message) {
        Action action;
        action.type = Action::Type::protocolViolation;
        action.error = message;
        return action;
    }

    uint8_t effectiveBlockSize() const {
        return localParameters.effectiveBlockSize(remoteParameters);
    }

    void resetSending() {
        sendingPayload.clear();
        sendingOffset = 0;
        nextSendSequence = 0;
        nextExpectedAckSequence = 0;
        retransmitRequestCount = 0;
        lastTransmittedBlock.clear();
    }

    void resetReceiving() {
        receivingPayload.clear();
        expectedReceiveLength = 0;
        nextReceiveSequence = 0;
    }

    Action handleConnectionSetup(const Frame& frame) {
        remoteParameters = frame.parameters();
        resetSending();
        resetReceiving();
        state = State::connected;
        return framesAction({ Frame::connectionAcknowledge(localParameters) }, 0, Event::connected);
    }

    Action handleConnectionAcknowledge(const Frame& frame) {
        remoteParameters = frame.parameters();
        if (state == State::awaitingConnectionAcknowledge || state == State::disconnected) {
            state = State::connected;
            return eventAction(Event::connected);
        }
        return eventAction(Event::connectionTest);
    }

    Action handleConnectionTest() {
        if (!isConnected() && behavior == Behavior::strict) {
            return protocolViolation("Unexpected TP2.0 connection test.");
        }

        if (state == State::disconnected) {
            state = State::connected;
        }
        return framesAction({ Frame::connectionAcknowledge(localParameters) }, 0, Event::connectionTest);
    }

    Action handleDisconnect() {
        reset();
        return eventAction(Event::disconnected);
    }

    Action handleAcknowledge(const Frame& frame) {
        if (lastTransmittedBlock.empty()) {
            return behavior == Behavior::defensive ? waitAction() : protocolViolation("Unexpected TP2.0 acknowledge.");
        }

        auto requestedSequence = frame.sequenceNumber();
        auto receiverReady = frame.receiverReady();

        if (requestedSequence == nextExpectedAckSequence) {
            lastTransmittedBlock.clear();
            retransmitRequestCount = 0;

            if (!receiverReady) {
                auto action = emitNextBlock();
                action.waitTimeMs = defaultWaitTimeMs;
                return action;
            }

            if (sendingOffset >= sendingPayload.size()) {
                resetSending();
                state = State::connected;
                return waitAction();
            }

            return emitNextBlock();
        }

        return retransmitBlockFrom(requestedSequence, receiverReady);
    }

    Action retransmitBlockFrom(uint8_t sequenceNumber, bool receiverReady) {
        auto it = std::find_if(lastTransmittedBlock.begin(), lastTransmittedBlock.end(), [sequenceNumber](const Frame& frame) {
            return frame.sequenceNumber() == sequenceNumber;
        });

        if (it == lastTransmittedBlock.end()) {
            return behavior == Behavior::defensive ? waitAction() : protocolViolation("Acknowledge requested an unknown TP2.0 sequence number.");
        }

        retransmitRequestCount += 1;
        if (retransmitRequestCount > maximumRetransmitRequests) {
            return protocolViolation("TP2.0 retransmit request limit exceeded.");
        }

        std::deque<Frame> frames;
        while (it != lastTransmittedBlock.end()) {
            frames.push_back(*it++);
        }

        return framesAction(std::move(frames), receiverReady ? 0 : defaultWaitTimeMs);
    }

    Action handleData(const Frame& frame) {
        std::deque<Frame> acknowledgements;
        auto payload = frame.payload();

        if (expectedReceiveLength == 0) {
            if (payload.size() < 2) {
                return behavior == Behavior::defensive ? waitAction() : protocolViolation("Initial TP2.0 data frame is missing the message length.");
            }

            expectedReceiveLength = static_cast<size_t>(payload[0] << 8 | payload[1]);
            receivingPayload = Bytes(payload.begin() + 2, payload.end());
            nextReceiveSequence = static_cast<uint8_t>((frame.sequenceNumber() + 1) & 0x0F);
        } else {
            if (frame.sequenceNumber() != nextReceiveSequence) {
                acknowledgements.push_back(Frame::acknowledge(nextReceiveSequence, true));
                return framesAction(std::move(acknowledgements));
            }

            receivingPayload.insert(receivingPayload.end(), payload.begin(), payload.end());
            nextReceiveSequence = static_cast<uint8_t>((nextReceiveSequence + 1) & 0x0F);
        }

        state = State::receiving;

        if (frame.acknowledgeRequested()) {
            acknowledgements.push_back(Frame::acknowledge(nextReceiveSequence, true));
        }

        auto complete = expectedReceiveLength > 0 &&
                        (receivingPayload.size() >= expectedReceiveLength || frame.endOfMessage());
        if (!complete) {
            if (!acknowledgements.empty()) {
                return framesAction(std::move(acknowledgements));
            }
            return waitAction();
        }

        auto processed = receivingPayload;
        if (processed.size() > expectedReceiveLength) {
            processed.resize(expectedReceiveLength);
        }

        resetReceiving();
        state = State::connected;

        if (!acknowledgements.empty()) {
            return processAndFramesAction(std::move(processed), std::move(acknowledgements));
        }
        return processAction(std::move(processed));
    }

    Action emitNextBlock() {
        if (sendingOffset >= sendingPayload.size()) {
            state = State::connected;
            return waitAction();
        }

        auto blockSize = effectiveBlockSize();
        auto framesToSend = std::deque<Frame>();
        auto bytesRemaining = sendingPayload.size() - sendingOffset;

        for (uint8_t frameIndex = 0; frameIndex < blockSize && bytesRemaining > 0; ++frameIndex) {
            Bytes payload;
            auto firstFrame = sendingOffset == 0;
            if (firstFrame) {
                vector_append_uint16(payload, static_cast<uint16_t>(sendingPayload.size()));
            }

            auto capacity = firstFrame ? firstFrameDataCapacity : consecutiveFrameDataCapacity;
            auto chunkSize = std::min<size_t>(capacity, bytesRemaining);
            payload.insert(payload.end(),
                           sendingPayload.begin() + static_cast<std::ptrdiff_t>(sendingOffset),
                           sendingPayload.begin() + static_cast<std::ptrdiff_t>(sendingOffset + chunkSize));
            sendingOffset += chunkSize;
            bytesRemaining -= chunkSize;

            auto endOfMessage = bytesRemaining == 0;
            auto acknowledgeRequested = endOfMessage || frameIndex + 1 == blockSize;
            auto frame = Frame::data(nextSendSequence, acknowledgeRequested, endOfMessage, payload);
            framesToSend.push_back(frame);
            nextSendSequence = static_cast<uint8_t>((nextSendSequence + 1) & 0x0F);

            if (acknowledgeRequested) {
                nextExpectedAckSequence = nextSendSequence;
                break;
            }
        }

        lastTransmittedBlock = framesToSend;
        state = State::sending;
        return framesAction(std::move(framesToSend), 0, Event::none, static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, timingByteToMicroseconds(remoteParameters.t3))));
    }
};

} // namespace TP20

} // namespace CANyonero

#endif
