#pragma once
#include "../../app_model/fakes/esp_log.h"
#define ESP_LOGV(...) ::rodakos_test::IgnoreLog(__VA_ARGS__)
