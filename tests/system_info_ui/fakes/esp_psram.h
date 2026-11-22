#pragma once
#include <cstddef>
// Physical chip size, like esp_psram_get_size() on the N16R8 board.
inline size_t& fake_psram_size() { static size_t size = 8U * 1024U * 1024U; return size; }
inline size_t esp_psram_get_size() { return fake_psram_size(); }
