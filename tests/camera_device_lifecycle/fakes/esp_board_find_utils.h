#pragma once
#include <string.h>
#include "esp_board_device.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const esp_board_device_desc_t g_esp_board_devices[];
extern esp_board_device_handle_t g_esp_board_device_handles[];
static inline esp_board_device_handle_t* esp_board_find_device_handle(const char* name) { for (esp_board_device_handle_t* h=g_esp_board_device_handles; h && h->name; h=h->next) if (strcmp(h->name,name)==0) return h; return NULL; }
static inline const esp_board_device_desc_t* esp_board_find_device_desc(const char* name) { for (const esp_board_device_desc_t* d=g_esp_board_devices; d && d->name; d=d->next) if (strcmp(d->name,name)==0) return d; return NULL; }
#ifdef __cplusplus
}
#endif
