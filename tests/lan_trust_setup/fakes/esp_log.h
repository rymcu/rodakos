#pragma once
#include <stdio.h>
#define ESP_LOG_DEBUG 4
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buffer, length, level)     ((void)(tag), (void)(buffer), (void)(length), (void)(level))
