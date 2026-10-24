#pragma once

// Host stand-in for ESP-SR's WebRTC AGC; see wake_frontend_runtime.cc.
typedef enum {
    AGC_MODE_SR = -1,
    AGC_MODE_0 = 0,
    AGC_MODE_1 = 1,
    AGC_MODE_2 = 2,
    AGC_MODE_3 = 3,
} agc_mode_t;
void* esp_agc_open(agc_mode_t agc_mode, int sample_rate);
void set_agc_config(void* agc_handle, int gain_dB, int limiter_enable, int target_level_dbfs);
int esp_agc_process(void* agc_handle, short* in_pcm, short* out_pcm, int frame_size, int sample_rate);
void esp_agc_close(void* agc_handle);
