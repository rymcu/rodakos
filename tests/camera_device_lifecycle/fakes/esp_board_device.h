#pragma once
#include "esp_err.h"
#include "esp_board_manager_err.h"
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
static inline int esp_board_extra_func_get(const char* type, void** function) { (void)type; if (function) *function = 0; return -1; }
typedef esp_err_t (*esp_board_device_init_func)(void*, int, void**);
typedef esp_err_t (*esp_board_device_deinit_func)(void*);
typedef int (*esp_board_device_power_ctrl_func)(void*, const char*, bool);
typedef esp_err_t (*esp_board_device_callback_register_func)(void*, const void*, int, void*, void*);
typedef struct esp_board_device_desc { const struct esp_board_device_desc* next; const char* name; const char* chip; const char* type; const char* sub_type; const void* cfg; uint16_t cfg_size; uint8_t init_skip; const char* power_ctrl_device; const char* const* depends_on; uint8_t depends_on_num; } esp_board_device_desc_t;
typedef struct esp_board_device_handle { struct esp_board_device_handle* next; const char* name; const char* chip; const char* type; void* device_handle; uint8_t ref_count; esp_board_device_init_func init; esp_board_device_deinit_func deinit; } esp_board_device_handle_t;
esp_err_t esp_board_device_init(const char*);
esp_err_t esp_board_device_deinit(const char*);
esp_err_t esp_board_device_get_handle(const char*, void**);
esp_err_t esp_board_device_get_config_by_handle(void*, void**);
esp_err_t esp_board_device_get_config(const char*, void**);
esp_err_t esp_board_device_power_ctrl(const char*, bool);
const esp_board_device_handle_t* esp_board_device_find_by_handle(void*);
#ifdef __cplusplus
}
#endif
