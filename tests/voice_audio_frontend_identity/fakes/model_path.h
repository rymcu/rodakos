#pragma once
#include "esp_mn_models.h"
#define ESP_MN_PREFIX "mn"
srmodel_list_t* srmodel_load(const void* address);
char* esp_srmodel_filter(srmodel_list_t* models, const char* prefix, const char* language);
void esp_srmodel_deinit(srmodel_list_t* models);
