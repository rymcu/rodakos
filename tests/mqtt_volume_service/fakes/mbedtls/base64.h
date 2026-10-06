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
inline int mbedtls_base64_decode(unsigned char*, size_t, size_t*, const unsigned char*, size_t) { return -1; }
