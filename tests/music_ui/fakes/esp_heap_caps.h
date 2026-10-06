#pragma once
#include "../../home_ui/fakes/esp_heap_caps.h"
inline void* heap_caps_malloc(size_t size, unsigned) { return std::malloc(size); }
