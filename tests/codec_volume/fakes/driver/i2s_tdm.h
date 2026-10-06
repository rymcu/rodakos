#pragma once
#include "i2s_std.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int i2s_tdm_slot_mask_t;
typedef struct { int data_bit_width; int slot_bit_width; int slot_mask; int total_slot; bool left_align; } i2s_tdm_slot_config_t;
typedef i2s_std_clk_config_t i2s_tdm_clk_config_t;
#define I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(bits, channels, mask) ((i2s_tdm_slot_config_t){bits,bits,mask,channels,false})
#define I2S_TDM_CLK_DEFAULT_CONFIG(rate) ((i2s_tdm_clk_config_t){rate,0,256})
esp_err_t i2s_channel_reconfig_tdm_slot(i2s_chan_handle_t, const i2s_tdm_slot_config_t*);
esp_err_t i2s_channel_reconfig_tdm_clock(i2s_chan_handle_t, const i2s_tdm_clk_config_t*);
#ifdef __cplusplus
}
#endif
