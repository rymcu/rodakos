#pragma once
#include <cstddef>
inline int mbedtls_base64_encode(unsigned char* output, size_t capacity, size_t* written,
                               const unsigned char* input, size_t size) {
    constexpr const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t length = 4 * ((size + 2) / 3);
    *written = length + 1;
    if (capacity < length + 1 || output == nullptr) return -1;
    size_t cursor = 0;
    for (size_t index = 0; index < size; index += 3) {
        const unsigned value = (unsigned(input[index]) << 16) |
            (index + 1 < size ? unsigned(input[index + 1]) << 8 : 0) |
            (index + 2 < size ? unsigned(input[index + 2]) : 0);
        output[cursor++] = alphabet[(value >> 18) & 63];
        output[cursor++] = alphabet[(value >> 12) & 63];
        output[cursor++] = index + 1 < size ? alphabet[(value >> 6) & 63] : '=';
        output[cursor++] = index + 2 < size ? alphabet[value & 63] : '=';
    }
    output[cursor] = 0;
    *written = length;
    return 0;
}
inline int mbedtls_base64_decode(unsigned char* output, size_t capacity, size_t* written,
                               const unsigned char* input, size_t size) {
    if (size == 0 || size % 4 != 0) return -1;
    const size_t padding = (input[size - 1] == '=') + (input[size - 2] == '=');
    const size_t length = (size / 4) * 3 - padding;
    *written = length;
    if (output == nullptr || capacity < length) return -1;
    const auto digit = [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') return int(ch - 'A');
        if (ch >= 'a' && ch <= 'z') return int(ch - 'a') + 26;
        if (ch >= '0' && ch <= '9') return int(ch - '0') + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };
    size_t cursor = 0;
    for (size_t index = 0; index < size; index += 4) {
        unsigned value = 0;
        for (size_t part = 0; part < 4; ++part) {
            const size_t position = index + part;
            if (input[position] == '=') {
                if (position < size - padding || part < 2) return -1;
                value <<= 6;
            } else {
                const int decoded = digit(input[position]);
                if (decoded < 0 || position >= size - padding) return -1;
                value = (value << 6) | static_cast<unsigned>(decoded);
            }
        }
        for (int shift : {16, 8, 0})
            if (cursor < length) output[cursor++] = static_cast<unsigned char>(value >> shift);
    }
    return 0;
}
