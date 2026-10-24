#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_afe_config.h"

// Host stand-in for ESP-SR's synchronous AEC; see wake_frontend_runtime.cc. Like the SDK,
// the handle is an anonymous-struct typedef, so production code cannot forward-declare it.
typedef struct {
    int frame_size;
} afe_aec_handle_t;
constexpr int AFE_TYPE_SR = 0;
constexpr int AFE_MODE_LOW_COST = 0;
afe_aec_handle_t* afe_aec_create(const char* input_format, int filter_length, int type, int mode);
size_t afe_aec_process(afe_aec_handle_t* handle, const int16_t* indata, int16_t* outdata);
int afe_aec_get_chunksize(afe_aec_handle_t* handle);
void afe_aec_destroy(afe_aec_handle_t* handle);
