#pragma once
#include <cstdint>
using jpeg_enc_handle_t = void*;
using jpeg_error_t = int;
constexpr jpeg_error_t JPEG_ERR_OK = 0;
constexpr int JPEG_PIXEL_FORMAT_RGB888 = 1, JPEG_SUBSAMPLE_420 = 1;
struct jpeg_enc_config_t {
    int width = 0, height = 0, src_type = 0, subsampling = 0;
    uint8_t quality = 0;
    bool task_enable = false;
};
#define DEFAULT_JPEG_ENC_CONFIG() jpeg_enc_config_t{}
jpeg_error_t jpeg_enc_open(const jpeg_enc_config_t*, jpeg_enc_handle_t*);
jpeg_error_t jpeg_enc_process(jpeg_enc_handle_t, const uint8_t*, int, uint8_t*, int, int*);
void jpeg_enc_close(jpeg_enc_handle_t);
