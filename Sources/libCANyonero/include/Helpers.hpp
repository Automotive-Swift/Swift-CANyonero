#ifndef HELPERS_HPP
#define HELPERS_HPP
#pragma once

#include <vector>

inline void vector_append_uint32(std::vector<uint8_t>& vec, uint32_t value) {
    vec.push_back(static_cast<uint8_t>(value >> 24));
    vec.push_back(static_cast<uint8_t>(value >> 16));
    vec.push_back(static_cast<uint8_t>(value >> 8));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
}

inline void vector_append_uint16(std::vector<uint8_t>& vec, uint16_t value) {
    vec.push_back(static_cast<uint8_t>(value >> 8));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
}

inline uint32_t vector_read_uint32(std::vector<uint8_t>::const_iterator& it) {
    uint32_t val = (static_cast<uint32_t>(*it++) << 24);
    val |= (static_cast<uint32_t>(*it++) << 16);
    val |= (static_cast<uint32_t>(*it++) << 8);
    val |= static_cast<uint32_t>(*it++);
    return val;
}

inline std::vector<uint8_t> createVector8FromArray(const uint8_t* array, const size_t length) {
    return std::vector<uint8_t>(array, array + length);
};

namespace CANyonero {
    typedef uint8_t ChannelHandle;
    typedef uint8_t PeriodicMessageHandle;
    typedef std::vector<uint8_t> Bytes;
    using StdVectorOfUInt8 = std::vector<uint8_t>;
}

#endif
