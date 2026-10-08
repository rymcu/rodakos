#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <cstdint>
struct esp_afe_sr_data_t {};
struct afe_fetch_result_t {
    esp_err_t ret_value = ESP_FAIL;
    int16_t* data = nullptr;
    int data_size = 0;
    int vad_state = 0;
};
constexpr int VAD_SILENCE = 0;
constexpr int VAD_SPEECH = 1;
struct esp_afe_sr_iface_t {
    esp_afe_sr_data_t* (*create_from_config)(struct afe_config_t*);
    int (*get_feed_chunksize)(esp_afe_sr_data_t*);
    int (*get_feed_channel_num)(esp_afe_sr_data_t*);
    void (*destroy)(esp_afe_sr_data_t*);
    int (*feed)(esp_afe_sr_data_t*, int16_t*);
    afe_fetch_result_t* (*fetch_with_delay)(esp_afe_sr_data_t*, TickType_t);
    int (*get_fetch_chunksize)(esp_afe_sr_data_t*);
    int (*reset_buffer)(esp_afe_sr_data_t*);
    int (*reset_vad)(esp_afe_sr_data_t*);
    int (*get_fetch_channel_num)(esp_afe_sr_data_t*);
    int (*get_samp_rate)(esp_afe_sr_data_t*);
};
