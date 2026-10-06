#pragma once
#include <cstdint>
#include "esp_afe_sr_iface.h"
struct afe_config_t {
    int aec_mode = 0;
    int vad_mode = 0;
    const char* vad_model_name = nullptr;
    int vad_min_speech_ms = 0;
    int vad_min_noise_ms = 0;
    int vad_delay_ms = 0;
    bool vad_mute_playback = false;
    bool vad_init = false;
    bool aec_init = false;
    int memory_alloc_mode = 0;
};
constexpr int AFE_TYPE_VC = 1;
constexpr int AFE_MODE_HIGH_PERF = 2;
constexpr int AEC_MODE_VOIP_HIGH_PERF = 3;
constexpr int VAD_MODE_0 = 0;
constexpr int AFE_MEMORY_ALLOC_MORE_PSRAM = 1;
afe_config_t* afe_config_init(const char*, void*, int, int);
void afe_config_free(afe_config_t*);
const esp_afe_sr_iface_t* esp_afe_handle_from_config(afe_config_t*);
