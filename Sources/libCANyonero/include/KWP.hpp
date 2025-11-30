///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#ifndef KWP_HPP
#define KWP_HPP
#pragma once

#include "Helpers.hpp"

#include <string>
#include <vector>
#include <numeric>

namespace CANyonero {
namespace KWP {

/// A minimal KWP2000/K-Line frame helper.
struct Frame {
    Bytes bytes;

    explicit Frame(const Bytes& in) : bytes(in) {}

    bool checksumValid() const {
        if (bytes.size() < 4) { return false; }
        uint8_t sum = 0;
        for (size_t i = 0; i + 1 < bytes.size(); ++i) {
            sum = static_cast<uint8_t>(sum + bytes[i]);
        }
        return sum == bytes.back();
    }

    size_t payloadLength() const {
        if (bytes.empty()) { return 0; }
        const uint8_t fmt = bytes[0];
        return fmt & 0x0F; // low nibble
    }

    size_t expectedSize() const {
        return 3 + payloadLength() + 1; // fmt + tgt + src + payload + checksum
    }

    bool sizeValid() const {
        return bytes.size() == expectedSize();
    }

    uint8_t target() const { return bytes.size() >= 2 ? bytes[1] : 0; }
    uint8_t source() const { return bytes.size() >= 3 ? bytes[2] : 0; }

    const uint8_t* payload() const { return bytes.size() > 3 ? &bytes[3] : nullptr; }
};

/// A small KWP transceiver that merges chained frames, strips repeated
/// service/PID and sequence bytes, and validates basic structure.
///
/// Sequence number detection is done retroactively: when a second frame
/// arrives and we detect that byte[2] of frame 1 was 0x01 and byte[2] of
/// frame 2 is 0x02, we recognize this as a multi-frame sequence and strip
/// the sequence bytes. This avoids false positives when 0x01 appears as
/// regular data in single-frame responses.
class Transceiver {
public:
    enum class State { idle, receiving };
    struct Action {
        enum class Type {
            process,
            waitForMore,
            protocolViolation,
        };
        Type type;
        std::string error;
        Bytes data;
    };

    Transceiver(uint8_t expectedTarget = 0, uint8_t expectedSource = 0, size_t expectedLength = 0)
        : expectedTarget(expectedTarget), expectedSource(expectedSource), expectedLength(expectedLength) {}

    void reset() {
        state = State::idle;
        baseService = 0;
        basePid = 0;
        haveBase = false;
        firstFrameHadPotentialSeq = false;
        sequenceMode = false;
        expectedSeq = 0;
        buffer.clear();
    }

    void setExpectedLength(size_t len) { expectedLength = len; }

    Action feed(const Bytes& frameBytes) {
        if (frameBytes.empty()) {
            return violation("Incoming frame is empty.");
        }

        Frame frame(frameBytes);
        if (!frame.sizeValid()) {
            return violation("Frame size does not match length in format byte.");
        }
        if (!frame.checksumValid()) {
            return violation("Checksum invalid.");
        }
        if (expectedTarget && frame.target() != expectedTarget) {
            return violation("Unexpected target address.");
        }
        if (expectedSource && frame.source() != expectedSource) {
            return violation("Unexpected source address.");
        }

        const size_t payloadLen = frame.payloadLength();
        const uint8_t* payload = frame.payload();
        if (!payload && payloadLen > 0) {
            return violation("Missing payload.");
        }

        if (!haveBase && payloadLen >= 2) {
            // First frame: record service+PID, buffer all remaining data
            baseService = payload[0];
            basePid = payload[1];
            haveBase = true;
            append(baseService);
            append(basePid);
            // Remember if byte[2] was 0x01 (potential sequence start)
            firstFrameHadPotentialSeq = (payloadLen >= 3 && payload[2] == 0x01);
            appendPayload(payload, payloadLen, 2);
            state = State::receiving;
        } else if (haveBase) {
            if (payloadLen >= 2) {
                if (payload[0] != baseService || payload[1] != basePid) {
                    return violation("Base service/PID mismatch.");
                }
            }

            // Retroactive sequence detection on second frame
            if (!sequenceMode && firstFrameHadPotentialSeq && payloadLen >= 3 && payload[2] == 0x02) {
                // Confirmed: this is a multi-frame sequence starting at 0x01
                // Remove the 0x01 that was buffered from first frame
                if (buffer.size() > 2 && buffer[2] == 0x01) {
                    buffer.erase(buffer.begin() + 2);
                }
                sequenceMode = true;
                expectedSeq = 0x03;
                appendPayload(payload, payloadLen, 3);
            } else if (sequenceMode) {
                // Already in sequence mode, validate sequence number
                if (payloadLen >= 3) {
                    if (payload[2] != expectedSeq) {
                        return violation("Sequence number mismatch.");
                    }
                    expectedSeq = static_cast<uint8_t>(payload[2] + 1);
                    appendPayload(payload, payloadLen, 3);
                } else {
                    appendPayload(payload, payloadLen, 2);
                }
            } else {
                // Not in sequence mode, treat byte[2] as data
                appendPayload(payload, payloadLen, 2);
            }
            state = State::receiving;
        } else {
            // No base discovered; just append payload
            appendPayload(payload, payloadLen, 0);
            state = State::receiving;
        }

        if (expectedLength > 0 && buffer.size() >= expectedLength) {
            return finalizeInternal();
        }
        return { Action::Type::waitForMore, {}, {} };
    }

    Action finalize() {
        if (buffer.empty()) {
            return { Action::Type::waitForMore, {}, {} };
        }
        return finalizeInternal();
    }

private:
    State state = State::idle;
    uint8_t expectedTarget;
    uint8_t expectedSource;
    size_t expectedLength;

    uint8_t baseService = 0;
    uint8_t basePid = 0;
    bool haveBase = false;
    bool firstFrameHadPotentialSeq = false;
    bool sequenceMode = false;
    uint8_t expectedSeq = 0;
    Bytes buffer;

private:
    void append(uint8_t byte) {
        buffer.push_back(byte);
    }

    void appendPayload(const uint8_t* payload, size_t payloadLen, size_t startIndex) {
        if (!payload || startIndex >= payloadLen) { return; }
        buffer.insert(buffer.end(), payload + startIndex, payload + payloadLen);
    }

    Action finalizeInternal() {
        Action action{ Action::Type::process, {}, buffer };
        reset();
        return action;
    }

    Action violation(const std::string& message) {
        reset();
        return { Action::Type::protocolViolation, message, {} };
    }
};

} // namespace KWP
} // namespace CANyonero
#endif
