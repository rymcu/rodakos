#pragma once
#include <cstdint>
inline int esp_flash_get_size(void*, uint32_t* size) { *size = 16U * 1024U * 1024U; return 0; }
