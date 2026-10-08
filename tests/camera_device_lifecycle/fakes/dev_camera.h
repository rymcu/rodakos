#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { const char* i2c_name; unsigned i2c_freq; } dev_camera_sub_dvp_cfg;
typedef struct { const char* name; const char* type; const char* sub_type; struct { dev_camera_sub_dvp_cfg dvp; } sub_cfg; } dev_camera_config_t;
typedef struct { const char* dev_path; const char* meta_path; } dev_camera_handle_t;
esp_err_t dev_camera_init(void*, int, void**);
esp_err_t dev_camera_deinit(void*);
#ifdef __cplusplus
}
#endif
