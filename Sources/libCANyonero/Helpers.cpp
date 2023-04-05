#include "Helpers.hpp"

std::vector<uint8_t> createVector8FromArray(const uint8_t* array, const size_t length) {
    return std::vector<uint8_t>(array, array + length);
};
