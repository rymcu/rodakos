#pragma once
#include <cstdio>
#include <cstdint>
struct sdmmc_card_t {
    struct { uint64_t capacity = 2048; uint32_t sector_size = 512; } csd;
};
struct dev_fs_fat_handle_t { sdmmc_card_t* card; const char* mount_point; };
inline void sdmmc_card_print_info(FILE*, const sdmmc_card_t*) {}
