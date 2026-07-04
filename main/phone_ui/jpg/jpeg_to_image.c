#include <esp_check.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>

#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"

#include "jpeg_to_image.h"

#include <esp_log.h>

#define TAG "jpeg_to_image"

static size_t round_up_to_8(size_t value) {
    return (value + 7) & ~((size_t)7);
}

static void convert_rgb888_to_lvgl_rgb888(uint8_t* data, size_t len) {
    if (data == NULL) {
        return;
    }
    for (size_t i = 0; i + 2 < len; i += 3) {
        uint8_t red = data[i];
        data[i] = data[i + 2];
        data[i + 2] = red;
    }
}

static void calculate_scaled_size(size_t image_width, size_t image_height, size_t target_width, size_t target_height,
                                  uint16_t* scaled_width, uint16_t* scaled_height) {
    *scaled_width = 0;
    *scaled_height = 0;

    if (target_width == 0 || target_height == 0 || image_width == 0 || image_height == 0) {
        return;
    }
    if (image_width <= target_width && image_height <= target_height) {
        return;
    }

    size_t width = target_width;
    size_t height = (target_width * image_height + image_width - 1) / image_width;
    if (height < target_height) {
        height = target_height;
        width = (target_height * image_width + image_height - 1) / image_height;
    }

    const size_t min_width = round_up_to_8((image_width + 7) / 8);
    const size_t min_height = round_up_to_8((image_height + 7) / 8);
    width = round_up_to_8(MAX(width, min_width));
    height = round_up_to_8(MAX(height, min_height));

    if (width >= image_width || height >= image_height || width > UINT16_MAX || height > UINT16_MAX) {
        return;
    }

    *scaled_width = (uint16_t)width;
    *scaled_height = (uint16_t)height;
}

static esp_err_t resize_rgb888_cover(uint8_t** out, size_t* out_len, size_t* width, size_t* height, size_t* stride,
                                     size_t target_width, size_t target_height) {
    if (target_width == 0 || target_height == 0 || (*width == target_width && *height == target_height)) {
        return ESP_OK;
    }

    const size_t output_len = target_width * target_height * 3;
    uint8_t* output = jpeg_calloc_align(output_len, 16);
    if (output == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const float scale_w = (float)target_width / (float)(*width);
    const float scale_h = (float)target_height / (float)(*height);
    const float scale = MAX(scale_w, scale_h);
    const float visible_width = (float)target_width / scale;
    const float visible_height = (float)target_height / scale;
    const float src_x0 = ((float)(*width) - visible_width) * 0.5f;
    const float src_y0 = ((float)(*height) - visible_height) * 0.5f;

    for (size_t y = 0; y < target_height; ++y) {
        size_t src_y = (size_t)MIN((float)(*height - 1), MAX(0.0f, src_y0 + ((float)y + 0.5f) / scale));
        for (size_t x = 0; x < target_width; ++x) {
            size_t src_x = (size_t)MIN((float)(*width - 1), MAX(0.0f, src_x0 + ((float)x + 0.5f) / scale));
            const uint8_t* src_px = *out + src_y * (*stride) + src_x * 3;
            uint8_t* dst_px = output + (y * target_width + x) * 3;
            dst_px[0] = src_px[0];
            dst_px[1] = src_px[1];
            dst_px[2] = src_px[2];
        }
    }

    jpeg_free_align(*out);
    *out = output;
    *out_len = output_len;
    *width = target_width;
    *height = target_height;
    *stride = target_width * 3;
    return ESP_OK;
}

static esp_err_t decode_with_new_jpeg(const uint8_t* src, size_t src_len, uint8_t** out, size_t* out_len, size_t* width,
                                      size_t* height, size_t* stride, size_t target_width, size_t target_height) {
    esp_err_t ret = ESP_OK;
    jpeg_error_t jpeg_ret = JPEG_ERR_OK;
    uint8_t* out_buf = NULL;
    jpeg_dec_io_t jpeg_io = {0};
    jpeg_dec_header_info_t out_info = {0};
    jpeg_dec_header_info_t original_info = {0};
    int out_buf_len = 0;

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB888;
    config.rotate = JPEG_ROTATE_0D;

    jpeg_dec_handle_t jpeg_dec = NULL;
    jpeg_ret = jpeg_dec_open(&config, &jpeg_dec);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open JPEG decoder");
        ret = ESP_FAIL;
        goto jpeg_dec_failed;
    }

    jpeg_io.inbuf = (uint8_t*)src;
    jpeg_io.inbuf_len = (int)src_len;

    jpeg_ret = jpeg_dec_parse_header(jpeg_dec, &jpeg_io, &original_info);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to parse JPEG header");
        ret = ESP_ERR_INVALID_ARG;
        goto jpeg_dec_failed;
    }

    calculate_scaled_size(original_info.width, original_info.height, target_width, target_height, &config.scale.width,
                          &config.scale.height);
    if (config.scale.width != 0 && config.scale.height != 0) {
        jpeg_dec_close(jpeg_dec);
        jpeg_dec = NULL;
        ESP_LOGD(TAG, "Scale JPEG from %dx%d to %dx%d", original_info.width, original_info.height, config.scale.width,
                 config.scale.height);
        jpeg_ret = jpeg_dec_open(&config, &jpeg_dec);
        if (jpeg_ret != JPEG_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open scaled JPEG decoder");
            ret = ESP_FAIL;
            goto jpeg_dec_failed;
        }

        jpeg_io.inbuf = (uint8_t*)src;
        jpeg_io.inbuf_len = (int)src_len;
        jpeg_io.inbuf_remain = 0;
        jpeg_io.outbuf = NULL;
        jpeg_io.out_size = 0;
        jpeg_ret = jpeg_dec_parse_header(jpeg_dec, &jpeg_io, &out_info);
        if (jpeg_ret != JPEG_ERR_OK) {
            ESP_LOGE(TAG, "Failed to parse scaled JPEG header");
            ret = ESP_ERR_INVALID_ARG;
            goto jpeg_dec_failed;
        }
    } else {
        out_info = original_info;
    }

    jpeg_ret = jpeg_dec_get_outbuf_len(jpeg_dec, &out_buf_len);
    if (jpeg_ret != JPEG_ERR_OK || out_buf_len <= 0) {
        ESP_LOGE(TAG, "Failed to get JPEG output buffer length");
        ret = ESP_ERR_INVALID_SIZE;
        goto jpeg_dec_failed;
    }

    out_buf = jpeg_calloc_align(out_buf_len, 16);
    if (out_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for JPEG output buffer");
        ret = ESP_ERR_NO_MEM;
        goto jpeg_dec_failed;
    }

    jpeg_io.outbuf = out_buf;
    jpeg_ret = jpeg_dec_process(jpeg_dec, &jpeg_io);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to decode JPEG");
        ret = ESP_FAIL;
        goto jpeg_dec_failed;
    }

    *out = out_buf;
    out_buf = NULL;
    *out_len = (size_t)out_buf_len;
    *width = config.scale.width != 0 ? config.scale.width : (size_t)out_info.width;
    *height = config.scale.height != 0 ? config.scale.height : (size_t)out_info.height;
    *stride = *width * 3;
    if (*out_len != (*stride * *height) && *stride != 0 && (*out_len % *stride) == 0) {
        *height = *out_len / *stride;
    }
    ret = resize_rgb888_cover(out, out_len, width, height, stride, target_width, target_height);
    if (ret != ESP_OK) {
        if (*out != NULL) {
            jpeg_free_align(*out);
            *out = NULL;
        }
        goto jpeg_dec_failed;
    }
    convert_rgb888_to_lvgl_rgb888(*out, *out_len);
    jpeg_dec_close(jpeg_dec);
    jpeg_dec = NULL;

    return ret;

jpeg_dec_failed:
    if (jpeg_dec) {
        jpeg_dec_close(jpeg_dec);
        jpeg_dec = NULL;
    }
    if (out_buf) {
        jpeg_free_align(out_buf);
        out_buf = NULL;
    }

    *out = NULL;
    *out_len = 0;
    *width = 0;
    *height = 0;
    *stride = 0;
    return ret;
}

esp_err_t jpeg_to_image(const uint8_t* src, size_t src_len, uint8_t** out, size_t* out_len, size_t* width,
                        size_t* height, size_t* stride) {
    return jpeg_to_image_scaled(src, src_len, out, out_len, width, height, stride, 0, 0);
}

esp_err_t jpeg_to_image_scaled(const uint8_t* src, size_t src_len, uint8_t** out, size_t* out_len, size_t* width,
                               size_t* height, size_t* stride, size_t target_width, size_t target_height) {
    if (src == NULL || src_len == 0 || out == NULL || out_len == NULL || width == NULL || height == NULL ||
        stride == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    return decode_with_new_jpeg(src, src_len, out, out_len, width, height, stride, target_width, target_height);
}
