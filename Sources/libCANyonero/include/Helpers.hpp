#ifndef HELPERS_HPP
#define HELPERS_HPP
#pragma once

#include <iostream>
#include <iomanip>
#include <vector>

inline void vector_append_uint16(std::vector<uint8_t>& vec, uint16_t value) {
    vec.push_back(static_cast<uint8_t>(value >> 8));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
}

inline void vector_append_uint32(std::vector<uint8_t>& vec, uint32_t value) {
    vec.push_back(static_cast<uint8_t>(value >> 24));
    vec.push_back(static_cast<uint8_t>(value >> 16));
    vec.push_back(static_cast<uint8_t>(value >> 8));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
}

inline uint16_t vector_read_uint16(std::vector<uint8_t>::const_iterator& it) {
    uint16_t val = (static_cast<uint16_t>(*it++) << 8);
    val |= static_cast<uint16_t>(*it++);
    return val;
}

inline uint32_t vector_read_uint32(std::vector<uint8_t>::const_iterator& it) {
    uint32_t val = (static_cast<uint32_t>(*it++) << 24);
    val |= (static_cast<uint32_t>(*it++) << 16);
    val |= (static_cast<uint32_t>(*it++) << 8);
    val |= static_cast<uint32_t>(*it++);
    return val;
}

inline std::vector<uint8_t> vector_drop_first(std::vector<uint8_t>& vec, size_t n) {
    std::vector<uint8_t> removed_elements;
    for (int i = 0; i < n; i++) {
        removed_elements.push_back(vec[i]);
    }
    vec.erase(vec.begin(), vec.begin() + n);
    return removed_elements;
}

template<typename T>
inline std::vector<T> operator+(const std::vector<T>& vec1, const std::vector<T>& vec2) {
    std::vector<T> result = vec1;
    result.insert(result.end(), vec2.begin(), vec2.end());
    return result;
}

template<typename T>
inline void print_hex_vector(const std::vector<T>& vec) {
    std::cout << "[ ";
    for (const auto& elem : vec) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << static_cast<int>(elem) << " ";
    }
    std::cout << "]" << std::endl;
}

/// For Swift/C++ interoperability.
inline std::vector<uint8_t> createVector8FromArray(const uint8_t* array, const size_t length) {
    return std::vector<uint8_t>(array, array + length);
};

namespace CANyonero {
    typedef uint8_t ChannelHandle;
    typedef uint8_t PeriodicMessageHandle;
    typedef std::vector<uint8_t> Bytes;
    using StdVectorOfUInt8 = std::vector<uint8_t>;
    using SeparationTime = uint8_t;
}

#endif
