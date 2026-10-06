#pragma once
#include "esp_mn_iface.h"
struct srmodel_list_t { int num = 0; };
esp_mn_iface_t* esp_mn_handle_from_name(const char* name);
