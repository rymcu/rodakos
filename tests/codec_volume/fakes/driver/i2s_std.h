#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include <hal/i2s_types.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FakeI2sChannel *i2s_chan_handle_t;
enum { I2S_COMM_MODE_STD, I2S_COMM_MODE_TDM, I2S_COMM_MODE_PDM };
enum { I2S_DIR_TX, I2S_DIR_RX };
enum { I2S_STD_SLOT_BOTH = 3, I2S_SLOT_MODE_MONO = 1, I2S_SLOT_MODE_STEREO = 2,
       I2S_MCLK_MULTIPLE_384 = 384 };
typedef int i2s_std_slot_mask_t;
typedef struct { int mode; int dir; i2s_chan_handle_t pair_chan; } i2s_chan_info_t;
typedef struct { int data_bit_width; int slot_bit_width; int slot_mask; int slot_mode; } i2s_std_slot_config_t;
typedef struct { int sample_rate_hz; int clk_src; int mclk_multiple; } i2s_std_clk_config_t;
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, channels) ((i2s_std_slot_config_t){bits,bits,3,channels})
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) ((i2s_std_clk_config_t){rate,0,256})
esp_err_t i2s_channel_get_info(i2s_chan_handle_t, i2s_chan_info_t*);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_channel_reconfig_std_slot(i2s_chan_handle_t, const i2s_std_slot_config_t*);
esp_err_t i2s_channel_reconfig_std_clock(i2s_chan_handle_t, const i2s_std_clk_config_t*);
esp_err_t i2s_channel_read(i2s_chan_handle_t, void*, size_t, size_t*, uint32_t);
esp_err_t i2s_channel_write(i2s_chan_handle_t, const void*, size_t, size_t*, uint32_t);
#ifdef __cplusplus
}
#endif
