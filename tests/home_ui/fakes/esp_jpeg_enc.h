#pragma once

#include <cstddef>

using jpeg_enc_handle_t = void*;
using jpeg_error_t = int;

constexpr jpeg_error_t JPEG_ERR_OK = 0;
constexpr int JPEG_PIXEL_FORMAT_RGB888 = 0;
constexpr int JPEG_SUBSAMPLE_420 = 0;

struct jpeg_enc_config_t {
    int width = 0;
    int height = 0;
    int src_type = 0;
    int subsampling = 0;
    int quality = 0;
    bool task_enable = false;
};

inline jpeg_enc_config_t DEFAULT_JPEG_ENC_CONFIG() {
    return {};
}

inline jpeg_error_t jpeg_enc_open(const jpeg_enc_config_t*, jpeg_enc_handle_t*) {
    return -1;
}

inline jpeg_error_t jpeg_enc_process(jpeg_enc_handle_t, const void*, int, void*, int, int*) {
    return -1;
}

inline jpeg_error_t jpeg_enc_close(jpeg_enc_handle_t) {
    return JPEG_ERR_OK;
}
