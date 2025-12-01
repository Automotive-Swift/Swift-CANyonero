///
/// CANyonero. (C) 2022 - 2023 Dr. Michael 'Mickey' Lauer <mickey@vanille-media.de>
///
#ifndef KLINE_HPP
#define KLINE_HPP
#pragma once

#include "Helpers.hpp"

#include <string>
#include <vector>
#include <numeric>

namespace CANyonero {
namespace KLine {

/// Supported K-Line protocol modes.
enum class ProtocolMode {
    kwp,
    iso9141,
};

/// A minimal K-Line frame helper (used for both KWP2000 and ISO 9141-2).
struct Frame {
    Bytes bytes;
    ProtocolMode mode;

    explicit Frame(const Bytes& in, ProtocolMode mode) : bytes(in), mode(mode) {}

    size_t headerSize() const {
        return 3; // both modes use three header bytes, but their meaning differs
    }

    bool checksumValid() const {
        if (bytes.size() < 4) { return false; }
        uint8_t sum = 0;
        for (size_t i = 0; i + 1 < bytes.size(); ++i) {
            sum = static_cast<uint8_t>(sum + bytes[i]);
        }
        return sum == bytes.back();
    }

    size_t payloadLength() const {
        if (mode == ProtocolMode::kwp) {
            if (bytes.empty()) { return 0; }
            const uint8_t fmt = bytes[0];
            return fmt & 0x0F; // low nibble
        }
        if (bytes.size() <= headerSize()) { return 0; }
        const size_t payloadPlusChecksum = bytes.size() - headerSize();
        if (payloadPlusChecksum == 0) { return 0; }
        return payloadPlusChecksum - 1; // strip checksum
    }

    size_t expectedSize() const {
        if (mode == ProtocolMode::kwp) {
            return headerSize() + payloadLength() + 1; // fmt + tgt + src + payload + checksum
        }
        // ISO 9141 frames do not encode length; require a minimal header + checksum
        return headerSize() + 1;
    }

    bool sizeValid() const {
        if (mode == ProtocolMode::kwp) {
            return bytes.size() == expectedSize();
        }
        return bytes.size() >= expectedSize();
    }

    uint8_t target() const {
        if (mode == ProtocolMode::kwp) {
            return bytes.size() >= 2 ? bytes[1] : 0;
        }
        return !bytes.empty() ? bytes[0] : 0;
    }

    uint8_t source() const {
        if (mode == ProtocolMode::kwp) {
            return bytes.size() >= 3 ? bytes[2] : 0;
        }
        return bytes.size() >= 2 ? bytes[1] : 0;
    }

    const uint8_t* payload() const {
        const size_t h = headerSize();
        return bytes.size() > h ? &bytes[h] : nullptr;
    }
};

/// A small K-Line transceiver that merges chained frames, strips repeated
/// service/PID and sequence bytes (KWP mode), and validates basic structure.
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

    Transceiver(uint8_t expectedTarget = 0,
                uint8_t expectedSource = 0,
                size_t expectedLength = 0,
                ProtocolMode mode = ProtocolMode::kwp)
        : expectedTarget(expectedTarget), expectedSource(expectedSource), expectedLength(expectedLength), mode(mode) {}

    static Transceiver makeKWP(uint8_t expectedTarget = 0, uint8_t expectedSource = 0, size_t expectedLength = 0) {
        return Transceiver(expectedTarget, expectedSource, expectedLength, ProtocolMode::kwp);
    }

    static Transceiver makeISO9141(uint8_t expectedTarget = 0, uint8_t expectedSource = 0, size_t expectedLength = 0) {
        return Transceiver(expectedTarget, expectedSource, expectedLength, ProtocolMode::iso9141);
    }

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

        Frame frame(frameBytes, mode);
        if (!frame.sizeValid()) {
            const char* msg = mode == ProtocolMode::kwp
                ? "Frame size does not match length in format byte."
                : "Frame size invalid for ISO 9141 mode.";
            return violation(msg);
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

        if (mode == ProtocolMode::iso9141) {
            appendPayload(payload, payloadLen, 0);
            state = State::receiving;
            if (expectedLength > 0 && buffer.size() >= expectedLength) {
                return finalizeInternal();
            }
            return { Action::Type::waitForMore, {}, {} };
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
    ProtocolMode mode;

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

/// Compute additive checksum used by both KWP and ISO 9141-2.
inline uint8_t checksum(const Bytes& bytes) {
    uint8_t sum = 0;
    for (auto b : bytes) { sum = static_cast<uint8_t>(sum + b); }
    return sum;
}

/// Build a KWP2000 frame on K-Line (format byte + target + source + payload + checksum).
inline Bytes makeKwpFrame(uint8_t target, uint8_t source, const Bytes& payload, uint8_t formatPrefix = 0x80) {
    Bytes frame;
    const uint8_t fmt = static_cast<uint8_t>(formatPrefix | (payload.size() & 0x0F));
    frame.push_back(fmt);
    frame.push_back(target);
    frame.push_back(source);
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(checksum(frame));
    return frame;
}

/// Build an ISO 9141-2 frame (target + source + tester + payload + checksum).
inline Bytes makeIso9141Frame(uint8_t target, uint8_t source, uint8_t tester, const Bytes& payload) {
    Bytes frame;
    frame.push_back(target);
    frame.push_back(source);
    frame.push_back(tester);
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(checksum(frame));
    return frame;
}

/// Build one or more KWP frames. If the payload (service+pid+data) fits into a
/// single frame (<= 0x0F bytes), a single-frame vector is returned. Otherwise
/// the function emits a sequence of frames with sequence numbers inserted at
/// payload[2] (starting at 0x01) and chunks of `maxDataPerFrame` bytes.
inline std::vector<Bytes> makeKwpFrames(uint8_t target, uint8_t source, const Bytes& payload,
                                        uint8_t formatPrefix = 0x80, size_t maxDataPerFrame = 4) {
    std::vector<Bytes> frames;
    if (payload.size() <= 0x0F) {
        frames.push_back(makeKwpFrame(target, source, payload, formatPrefix));
        return frames;
    }
    if (payload.size() < 2) { return frames; } // need at least service+pid

    const uint8_t service = payload[0];
    const uint8_t pid = payload[1];
    const Bytes data(payload.begin() + 2, payload.end());
    size_t offset = 0;
    uint8_t seq = 0x01;
    const size_t chunkSize = maxDataPerFrame == 0 ? 4 : maxDataPerFrame;

    while (offset < data.size()) {
        size_t take = std::min(chunkSize, data.size() - offset);
        Bytes chunk;
        chunk.reserve(2 + 1 + take);
        chunk.push_back(service);
        chunk.push_back(pid);
        chunk.push_back(seq);
        chunk.insert(chunk.end(), data.begin() + offset, data.begin() + offset + take);
        frames.push_back(makeKwpFrame(target, source, chunk, formatPrefix));
        offset += take;
        seq = static_cast<uint8_t>(seq + 1);
    }
    return frames;
}

/// Split a contiguous K-Line buffer into individual frames based on protocol mode.
inline std::vector<Bytes> splitFrames(const uint8_t* buffer, size_t length, ProtocolMode mode) {
    std::vector<Bytes> frames;
    if (!buffer || length < 4) { return frames; }

    size_t index = 0;
    if (mode == ProtocolMode::kwp) {
        while (index + 4 <= length) {
            const uint8_t fmt = buffer[index];
            const size_t payloadLen = fmt & 0x0F;
            const size_t frameLen = 3 + payloadLen + 1;
            if (index + frameLen > length) { break; }
            frames.emplace_back(buffer + index, buffer + index + frameLen);
            index += frameLen;
        }
    } else {
        // ISO 9141: length not encoded; treat whole buffer as one frame.
        frames.emplace_back(buffer, buffer + length);
    }
    return frames;
}

inline std::vector<Bytes> splitFrames(const Bytes& raw, ProtocolMode mode) {
    return splitFrames(raw.data(), raw.size(), mode);
}

/// Convenience: decode a raw byte stream into a payload using the K-Line transceiver.
inline Bytes decodeStream(const Bytes& raw, ProtocolMode mode,
                          uint8_t expectedTarget = 0, uint8_t expectedSource = 0, size_t expectedLength = 0) {
    Transceiver trx(expectedTarget, expectedSource, expectedLength, mode);
    auto frames = splitFrames(raw, mode);
    for (const auto& f : frames) {
        auto action = trx.feed(f);
        if (action.type == Transceiver::Action::Type::process) { return action.data; }
        if (action.type == Transceiver::Action::Type::protocolViolation) { return {}; }
    }
    auto finalAction = trx.finalize();
    return finalAction.type == Transceiver::Action::Type::process ? finalAction.data : Bytes();
}

} // namespace KLine
} // namespace CANyonero
#endif
