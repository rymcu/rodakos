#pragma once
#include "nvs.h"
#include <cstddef>
#include <cstdint>
#include <vector>

constexpr int ESP_FAIL = -1;
constexpr int ESP_PARTITION_TYPE_APP = 0;
constexpr int ESP_PARTITION_SUBTYPE_APP_FACTORY = 0;
constexpr int ESP_PARTITION_SUBTYPE_APP_OTA_0 = 16;
constexpr int ESP_IMAGE_VERIFY_SILENT = 0;
struct esp_partition_t { uint32_t address, size; int subtype; const char* label; };
struct esp_partition_pos_t { uint32_t offset, size; };
struct esp_image_metadata_t {};
using esp_ota_handle_t = uint32_t;
namespace fake_recovery {
inline esp_partition_t app{0x2a0000, 0xd50000, 16, "app"};
inline esp_partition_t recovery{0x20000, 0x280000, 0, "recovery"};
inline const esp_partition_t* selected = &recovery;
inline bool invalid = false;
inline bool valid = true;
inline int erases = 0;
inline std::vector<uint8_t> flashed;
inline void Reset() { selected = &recovery; invalid = false; valid = true; erases = 0; flashed.clear(); }
}
inline const char* esp_err_to_name(int) { return "fake-error"; }
inline const esp_partition_t* esp_partition_find_first(int, int subtype, const char*) {
    return subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? &fake_recovery::app : &fake_recovery::recovery;
}
inline const esp_partition_t* esp_ota_get_running_partition() { return &fake_recovery::recovery; }
inline const esp_partition_t* esp_ota_get_last_invalid_partition() { return fake_recovery::invalid ? &fake_recovery::app : nullptr; }
inline int esp_image_verify(int, const esp_partition_pos_t*, esp_image_metadata_t*) {
    return fake_recovery::valid ? ESP_OK : ESP_FAIL;
}
inline int esp_ota_set_boot_partition(const esp_partition_t* part) { fake_recovery::selected = part; return ESP_OK; }
inline int esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* handle) {
    *handle = 1; ++fake_recovery::erases; fake_recovery::flashed.clear(); fake_recovery::valid = false; return ESP_OK;
}
inline int esp_ota_write(esp_ota_handle_t, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    fake_recovery::flashed.insert(fake_recovery::flashed.end(), bytes, bytes + size); return ESP_OK;
}
inline int esp_ota_end(esp_ota_handle_t) { fake_recovery::valid = true; return ESP_OK; }
inline void esp_ota_abort(esp_ota_handle_t) {}
struct sdmmc_card_t {};
struct sdmmc_host_t {};
struct sdmmc_slot_config_t { int width = 0, clk = 0, cmd = 0, d0 = 0, flags = 0; };
struct esp_vfs_fat_sdmmc_mount_config_t { bool format_if_mount_failed; int max_files, allocation_unit_size; };
constexpr int GPIO_NUM_47 = 47, GPIO_NUM_48 = 48, GPIO_NUM_21 = 21;
constexpr int SDMMC_SLOT_FLAG_INTERNAL_PULLUP = 1;
#define SDMMC_HOST_DEFAULT() sdmmc_host_t{}
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
inline int esp_vfs_fat_sdmmc_mount(const char*, const sdmmc_host_t*, const sdmmc_slot_config_t*,
                                  const esp_vfs_fat_sdmmc_mount_config_t*, sdmmc_card_t** card) {
    static sdmmc_card_t mounted; *card = &mounted; return ESP_OK;
}
