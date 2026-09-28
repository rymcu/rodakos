#include "rodak_ota_state.h"
#include "rodak_ota_signature.h"
#include "rodak_release_fault.h"
#include "rodak_sha256.h"

#include <driver/sdmmc_host.h>
#include <esp_err.h>
#include <esp_image_format.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <sdmmc_cmd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {
constexpr const char* TAG = "RodakRecovery";
constexpr const char* kMountPoint = "/sdcard";
constexpr size_t kCopyBufferSize = 32 * 1024;



struct MountedSdCard {
    std::string mount_point;
    sdmmc_card_t* card = nullptr;
    bool mounted = false;
};


bool InitializeNvs() {
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Default NVS is unavailable; preserving device data: %s",
                 esp_err_to_name(err));
    }
    if (!rodakos::InitializeOtaUpdateState()) {
        ESP_LOGE(TAG, "OTA state partition initialization failed");
        return false;
    }
    return true;
}

const esp_partition_t* MainPartition() {
    return esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
}

bool IsValidImage(const esp_partition_t* partition) {
    if (partition == nullptr) {
        return false;
    }
    const esp_partition_pos_t position = {
        .offset = partition->address,
        .size = partition->size,
    };
    esp_image_metadata_t metadata = {};
    return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &position, &metadata) == ESP_OK;
}

bool MountSdCard(MountedSdCard& sd_card) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = GPIO_NUM_47;
    slot.cmd = GPIO_NUM_48;
    slot.d0 = GPIO_NUM_21;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 4;
    mount_config.allocation_unit_size = 16 * 1024;
    const esp_err_t err = esp_vfs_fat_sdmmc_mount(
        kMountPoint, &host, &slot, &mount_config, &sd_card.card);
    if (err != ESP_OK || sd_card.card == nullptr) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(err));
        return false;
    }

    sd_card.mount_point = kMountPoint;
    sd_card.mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", sd_card.mount_point.c_str());
    return true;
}

std::string AbsolutePath(const MountedSdCard& sd_card, const char* relative_path) {
    return sd_card.mount_point + relative_path;
}

bool VerifyFile(const std::string& path, uint64_t expected_size,
                const std::string& expected_sha256) {
    return rodakos::VerifyOtaImageFile(path, expected_size, expected_sha256);
}

bool VerifyPendingSignature(const MountedSdCard& sd_card,
                            const rodakos::OtaUpdateRecord& record) {
    std::string signature;
    const bool verified = rodakos::ReadOtaSignatureFile(
        AbsolutePath(sd_card, rodakos::kOtaPendingSignaturePath), signature) &&
        rodakos::VerifyOtaSignature(record, signature);
    if (!verified) {
        ESP_LOGE(TAG, "Pending OTA signature verification failed; no candidate write");
    }
    return verified;
}

bool WriteImage(const std::string& path, uint64_t image_size, const std::string& expected_sha256, bool restoring = false) {
    const esp_partition_t* partition = MainPartition();
    if (partition == nullptr || image_size == 0 || image_size > partition->size) {
        ESP_LOGE(TAG, "Main partition cannot hold image (%llu bytes)",
                 static_cast<unsigned long long>(image_size));
        return false;
    }

    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Cannot open image for flashing: %s", path.c_str());
        return false;
    }

    std::unique_ptr<unsigned char[]> buffer(
        new (std::nothrow) unsigned char[kCopyBufferSize]);
    if (buffer == nullptr) {
        std::fclose(file);
        ESP_LOGE(TAG, "Cannot allocate image flashing buffer");
        return false;
    }
    rodakos::Sha256 sha;
    if (!sha.Start()) {
        std::fclose(file);
        return false;
    }
    rodakos::OtaFaultPoint(restoring ? "before_restore_erase" : "before_image_erase");
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(partition, static_cast<size_t>(image_size), &ota_handle);
    if (err != ESP_OK) {
        std::fclose(file);
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return false;
    }
    rodakos::OtaFaultPoint(restoring ? "after_restore_erase" : "after_image_erase");
    uint64_t written = 0;
    while (written < image_size) {
        const size_t requested = static_cast<size_t>(
            std::min<uint64_t>(kCopyBufferSize, image_size - written));
        const size_t read = std::fread(buffer.get(), 1, requested, file);
        if (read != requested || !sha.Update(buffer.get(), read)) {
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(ota_handle, buffer.get(), read);
        if (err != ESP_OK) {
            break;
        }
        written += read;
        if (written == read) {
            rodakos::OtaFaultPoint(restoring ? "during_restore_write" : "during_image_write");
        }
        if (written % (1024 * 1024) < kCopyBufferSize) {
            ESP_LOGI(TAG, "Flashed %llu/%llu bytes",
                     static_cast<unsigned long long>(written),
                     static_cast<unsigned long long>(image_size));
        }
    }
    std::array<unsigned char, 32> digest = {};
    if (err == ESP_OK && (std::fgetc(file) != EOF || std::ferror(file) ||
                         !sha.Finish(digest) || rodakos::Sha256ToHex(digest) != expected_sha256)) {
        err = ESP_FAIL;
    }
    std::fclose(file);

    if (err == ESP_OK) {
        err = esp_ota_end(ota_handle);
    } else {
        esp_ota_abort(ota_handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Image flashing failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

[[noreturn]] void RestartInto(const esp_partition_t* partition) {
    const esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select main partition: %s", esp_err_to_name(err));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    ESP_LOGI(TAG, "Restarting into %s", partition->label);
    esp_restart();
}

[[noreturn]] void RestartRestoredImage(rodakos::OtaUpdateRecord& record) {
    const esp_partition_t* partition = MainPartition();
    if (!IsValidImage(partition)) {
        ESP_LOGE(TAG, "Restored image is not bootable; staying in Recovery");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    const esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select restored image: %s", esp_err_to_name(err));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    record.phase = rodakos::OtaUpdatePhase::kRollbackBooting;
    if (!rodakos::SaveOtaUpdateRecord(record)) {
        ESP_LOGE(TAG, "Rollback boot marker was not saved; staying in Recovery");
        const esp_partition_t* recovery = esp_ota_get_running_partition();
        if (recovery == nullptr || esp_ota_set_boot_partition(recovery) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restore Recovery as the boot partition");
        }
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    ESP_LOGI(TAG, "Restarting into restored main image");
    esp_restart();
}

bool RestoreInstalledImage(const MountedSdCard& sd_card, rodakos::OtaUpdateRecord& record) {
    if (record.installed_size == 0 || record.installed_sha256.empty()) {
        ESP_LOGE(TAG, "No installed-image backup is available");
        return false;
    }

    const bool boot_rollback = record.error_code.empty() || record.error_code == "BOOT_ROLLBACK";
    record.phase = rodakos::OtaUpdatePhase::kRestoring;
    record.detail = boot_rollback ? "Recovery 正在恢复上一版本"
                                  : "新固件应用失败，Recovery 正在恢复上一版本";
    if (boot_rollback) {
        record.error_code = "BOOT_ROLLBACK";
    }
    if (!rodakos::SaveOtaUpdateRecord(record)) {
        ESP_LOGE(TAG, "Failed to persist restoring state; refusing to modify the main partition");
        return false;
    }
    rodakos::OtaFaultPoint("after_restore_state");

    const std::string path = AbsolutePath(sd_card, rodakos::kOtaInstalledImagePath);
    if (!VerifyFile(path, record.installed_size, record.installed_sha256) ||
        !WriteImage(path, record.installed_size, record.installed_sha256, true)) {
        return false;
    }
    rodakos::OtaFaultPoint("after_restore_write");

    record.phase = rodakos::OtaUpdatePhase::kRollbackReadyToBoot;
    record.detail = "上一版本已恢复，等待启动确认";
    if (!rodakos::SaveOtaUpdateRecord(record)) {
        ESP_LOGE(TAG, "Failed to persist rollback-ready state; staying in Recovery");
        return false;
    }
    rodakos::OtaFaultPoint("after_rollback_ready");
    RestartRestoredImage(record);
}
}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting immutable SD recovery runtime");
    ESP_LOGI(TAG, "%s", rodakos::OtaTrustMarker());
    if (!InitializeNvs()) {
        return;
    }

    rodakos::OtaUpdateRecord record;
    const rodakos::OtaUpdateLoadResult load_result =
        rodakos::LoadOtaUpdateRecordStatus(record);
    if (load_result == rodakos::OtaUpdateLoadResult::kCorrupt ||
        load_result == rodakos::OtaUpdateLoadResult::kError) {
        ESP_LOGE(TAG, "OTA state is corrupt or unavailable; staying in Recovery");
        return;
    }
    const bool has_update = load_result == rodakos::OtaUpdateLoadResult::kValid;
    const esp_partition_t* main_partition = MainPartition();
    const esp_partition_t* last_invalid = esp_ota_get_last_invalid_partition();
    const bool main_was_invalid = main_partition != nullptr && last_invalid != nullptr &&
                                  last_invalid->address == main_partition->address;
    if (!has_update || record.phase == rodakos::OtaUpdatePhase::kIdle ||
        record.phase == rodakos::OtaUpdatePhase::kAwaitingStagedAck ||
        record.phase == rodakos::OtaUpdatePhase::kConfirmed ||
        record.phase == rodakos::OtaUpdatePhase::kFailed ||
        record.phase == rodakos::OtaUpdatePhase::kReportAcknowledged) {
        if (main_was_invalid) {
            ESP_LOGE(TAG, "Bootloader rejected the main image; refusing an automatic retry");
            return;
        }
        if (IsValidImage(main_partition)) {
            RestartInto(main_partition);
        }
        ESP_LOGW(TAG, "No bootable main image is installed");
        return;
    }

    if (record.phase == rodakos::OtaUpdatePhase::kRollbackBooting) {
        ESP_LOGE(TAG, "Bootloader rejected the restored image; staying in Recovery");
        return;
    }
    if (record.phase == rodakos::OtaUpdatePhase::kRollbackReadyToBoot) {
        ESP_LOGI(TAG, "Resuming restored-image boot handoff");
        RestartRestoredImage(record);
    }
    if (record.phase == rodakos::OtaUpdatePhase::kRollbackPending) {
        if (IsValidImage(main_partition)) {
            RestartInto(main_partition);
        }
        ESP_LOGE(TAG, "Confirmed rollback image is no longer bootable");
        return;
    }
    if (record.phase == rodakos::OtaUpdatePhase::kReadyToBoot) {
        if (!main_was_invalid && IsValidImage(main_partition)) {
            ESP_LOGI(TAG, "Resuming the first boot of the newly written main image");
            RestartInto(main_partition);
        }
        if (record.error_code.empty()) {
            record.detail = main_was_invalid
                                ? "新固件启动失败，Recovery 准备恢复上一版本"
                                : "新固件镜像无效，Recovery 准备恢复上一版本";
            record.error_code = main_was_invalid ? "BOOT_ROLLBACK"
                                                  : "RECOVERY_IMAGE_INVALID";
            if (!rodakos::SaveOtaUpdateRecord(record)) {
                ESP_LOGE(TAG, "Failed to persist the rollback root cause");
                return;
            }
        }
    }

    MountedSdCard sd_card;
    if (!MountSdCard(sd_card)) {
        if (record.error_code.empty()) {
            record.detail = "Recovery 无法挂载 SD 卡";
            record.error_code = "SD_MOUNT_FAILED";
            if (!rodakos::SaveOtaUpdateRecord(record)) {
                ESP_LOGE(TAG, "Failed to persist SD mount failure");
            }
        }
        return;
    }

    if (record.phase == rodakos::OtaUpdatePhase::kReadyToBoot ||
        record.phase == rodakos::OtaUpdatePhase::kRestoring) {
        RestoreInstalledImage(sd_card, record);
        return;
    }

    const std::string pending_path = AbsolutePath(sd_card, rodakos::kOtaPendingImagePath);
    const bool previously_applying = record.phase == rodakos::OtaUpdatePhase::kApplying;
    const bool candidate_valid = VerifyPendingSignature(sd_card, record) &&
        VerifyFile(pending_path, record.pending_size, record.pending_sha256);
    if (!candidate_valid) {
        record.detail = "Recovery rejected candidate before erase";
        record.error_code = "RECOVERY_IMAGE_UNAUTHENTICATED";
        if (previously_applying) {
            RestoreInstalledImage(sd_card, record);
        } else {
            // A rejected pending image has never touched ota_0; preserve it byte for byte.
            record.phase = rodakos::OtaUpdatePhase::kFailed;
            if (rodakos::SaveOtaUpdateRecord(record) && !main_was_invalid && IsValidImage(main_partition)) {
                RestartInto(main_partition);
            }
        }
        return;
    }
    if (!VerifyFile(AbsolutePath(sd_card, rodakos::kOtaInstalledImagePath),
                    record.installed_size, record.installed_sha256)) {
        ESP_LOGE(TAG, "Rollback backup is unavailable; refusing candidate erase");
        return;
    }
    record.phase = rodakos::OtaUpdatePhase::kApplying;
    record.detail = "Recovery applying authenticated image";
    record.error_code.clear();
    if (!rodakos::SaveOtaUpdateRecord(record)) {
        ESP_LOGE(TAG, "Applying state not durable; refusing candidate erase");
        return;
    }
    rodakos::OtaFaultPoint("after_applying_state");
    if (!WriteImage(pending_path, record.pending_size, record.pending_sha256)) {
        record.detail = "Recovery 写入新固件失败";
        record.error_code = "RECOVERY_FLASH_FAILED";
        RestoreInstalledImage(sd_card, record);
        return;
    }
    rodakos::OtaFaultPoint("after_image_write");

    record.phase = rodakos::OtaUpdatePhase::kReadyToBoot;
    record.detail = "新固件已写入，等待首次启动确认";
    record.error_code.clear();
    if (!rodakos::SaveOtaUpdateRecord(record)) {
        ESP_LOGE(TAG, "Failed to persist verification state");
        return;
    }
    rodakos::OtaFaultPoint("after_ready_to_boot");
    RestartInto(main_partition);
}
