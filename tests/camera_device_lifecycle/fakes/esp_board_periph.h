#pragma once
#include "esp_err.h"
#include "esp_board_manager_defs.h"
typedef struct esp_board_periph_desc { const struct esp_board_periph_desc* next; const char* name; const char* type; esp_board_periph_role_t role; const char* format; const void* cfg; int cfg_size; int id; } esp_board_periph_desc_t;
typedef struct esp_board_periph_entry { struct esp_board_periph_entry* next; const char* type; esp_board_periph_role_t role; esp_err_t (*init)(void*, int, void**); esp_err_t (*deinit)(void*); } esp_board_periph_entry_t;
esp_err_t esp_board_periph_init_all(void);
esp_err_t esp_board_periph_deinit_all(void);
esp_err_t esp_board_periph_get_handle(const char*, void**);
esp_err_t esp_board_periph_get_config(const char*, void**);
