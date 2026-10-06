#pragma once
#include <cstdint>
struct model_iface_data_t {};
enum esp_mn_state_t { ESP_MN_STATE_DETECTING, ESP_MN_STATE_DETECTED, ESP_MN_STATE_TIMEOUT };
struct esp_mn_results_t {
    int num = 0;
    int command_id[8]{};
    const char* string = nullptr;
    float prob[8]{};
};
struct esp_mn_iface_t {
    model_iface_data_t* (*create)(const char*, int);
    void (*destroy)(model_iface_data_t*);
    void (*set_det_threshold)(model_iface_data_t*, float);
    int (*get_samp_chunksize)(model_iface_data_t*);
    esp_mn_state_t (*detect)(model_iface_data_t*, int16_t*);
    esp_mn_results_t* (*get_results)(model_iface_data_t*);
    void (*clean)(model_iface_data_t*);
};
