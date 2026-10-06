#pragma once
#include "esp_err.h"
#include "esp_mn_iface.h"
esp_err_t esp_mn_commands_alloc(esp_mn_iface_t*, model_iface_data_t*);
esp_err_t esp_mn_commands_clear();
esp_err_t esp_mn_commands_add(int id, const char* command);
const char* esp_mn_commands_update();
void esp_mn_commands_free();
