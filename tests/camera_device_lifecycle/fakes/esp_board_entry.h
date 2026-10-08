#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct esp_board_entry_desc { const char* entry_name; esp_err_t (*init_func)(void*, int, void**); esp_err_t (*deinit_func)(void*); } esp_board_entry_desc_t;
const esp_board_entry_desc_t* esp_board_entry_find_subtype_desc(const char*, const char*);
#ifdef __cplusplus
}
#endif
