#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef enum { ESP_BOARD_PERIPH_ROLE_NONE = 0 } esp_board_periph_role_t;
typedef struct { const char* name; const char* chip; const char* version; const char* description; const char* manufacturer; } esp_board_info_t;
