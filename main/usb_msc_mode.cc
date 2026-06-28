#include "usb_msc_mode.h"

#include <driver/gpio.h>
#include <driver/sdmmc_host.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <sdmmc_cmd.h>

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

#include <cstdlib>

namespace {
constexpr const char* TAG = "UsbMscMode";
constexpr const char* kMscNvsNamespace = "startup";
constexpr const char* kMscNvsKey = "usb_msc";
constexpr gpio_num_t kMscModeButtonGpio = GPIO_NUM_10;
constexpr int kMscExitHoldMs = 1500;

constexpr gpio_num_t kSdCardClk = GPIO_NUM_47;
constexpr gpio_num_t kSdCardCmd = GPIO_NUM_48;
constexpr gpio_num_t kSdCardD0 = GPIO_NUM_21;

tinyusb_msc_storage_handle_t g_storage_handle = nullptr;
sdmmc_card_t* g_card = nullptr;
bool g_usb_attached = false;

esp_err_t OpenMscNvs(nvs_handle_t* handle) {
    return nvs_open(kMscNvsNamespace, NVS_READWRITE, handle);
}

bool IsMscButtonPressed() {
    return gpio_get_level(kMscModeButtonGpio) == 0;
}

void UsbEventCallback(tinyusb_event_t* event, void* arg) {
    (void)arg;
    switch (event->id) {
        case TINYUSB_EVENT_ATTACHED:
            g_usb_attached = true;
            ESP_LOGI(TAG, "USB attached to host");
            break;
        case TINYUSB_EVENT_DETACHED:
            g_usb_attached = false;
            ESP_LOGI(TAG, "USB detached from host");
            break;
        default:
            break;
    }
}

void MscEventCallback(tinyusb_msc_storage_handle_t handle,
                      tinyusb_msc_event_t* event,
                      void* arg) {
    (void)handle;
    (void)arg;
    switch (event->id) {
        case TINYUSB_MSC_EVENT_MOUNT_START:
            ESP_LOGI(TAG, "MSC mount start");
            break;
        case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
            ESP_LOGI(TAG, "MSC mount complete");
            break;
        case TINYUSB_MSC_EVENT_MOUNT_FAILED:
            ESP_LOGE(TAG, "MSC mount failed");
            break;
        case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
            ESP_LOGW(TAG, "MSC format required");
            break;
        case TINYUSB_MSC_EVENT_FORMAT_FAILED:
            ESP_LOGE(TAG, "MSC format failed");
            break;
        default:
            break;
    }
}

sdmmc_card_t* InitSdCardForMsc() {
    ESP_LOGI(TAG, "Initializing SD card for USB MSC");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = kSdCardClk;
    slot_config.cmd = kSdCardCmd;
    slot_config.d0 = kSdCardD0;
    slot_config.d1 = GPIO_NUM_NC;
    slot_config.d2 = GPIO_NUM_NC;
    slot_config.d3 = GPIO_NUM_NC;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC host init failed: %s", esp_err_to_name(ret));
        return nullptr;
    }

    ret = sdmmc_host_init_slot(host.slot, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC slot init failed: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        return nullptr;
    }

    auto* card = static_cast<sdmmc_card_t*>(std::calloc(1, sizeof(sdmmc_card_t)));
    if (card == nullptr) {
        ESP_LOGE(TAG, "No memory for SD card");
        sdmmc_host_deinit();
        return nullptr;
    }

    ret = sdmmc_card_init(&host, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card init failed: %s", esp_err_to_name(ret));
        std::free(card);
        sdmmc_host_deinit();
        return nullptr;
    }

    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card ready for USB MSC");
    return card;
}
}  // namespace

bool CheckMscButtonAtStartup() {
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << kMscModeButtonGpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    vTaskDelay(pdMS_TO_TICKS(10));

    const bool pressed = IsMscButtonPressed();
    ESP_LOGI(TAG, "MSC startup button GPIO10=%d (%s)",
             gpio_get_level(kMscModeButtonGpio),
             pressed ? "pressed" : "released");
    return pressed;
}

bool RequestUsbMscModeOnNextBoot() {
    nvs_handle_t handle = 0;
    esp_err_t ret = OpenMscNvs(&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Open NVS for MSC request failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = nvs_set_u8(handle, kMscNvsKey, 1);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Store MSC boot request failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Stored one-shot USB MSC boot request");
    return true;
}

bool ConsumeUsbMscModeBootRequest() {
    nvs_handle_t handle = 0;
    esp_err_t ret = OpenMscNvs(&handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Open NVS for MSC consume failed: %s", esp_err_to_name(ret));
        return false;
    }

    uint8_t requested = 0;
    ret = nvs_get_u8(handle, kMscNvsKey, &requested);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return false;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read MSC boot request failed: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return false;
    }

    if (requested != 0) {
        esp_err_t clear_ret = nvs_erase_key(handle, kMscNvsKey);
        if (clear_ret == ESP_OK || clear_ret == ESP_ERR_NVS_NOT_FOUND) {
            clear_ret = nvs_commit(handle);
        }
        if (clear_ret != ESP_OK) {
            ESP_LOGW(TAG, "Clear MSC boot request failed: %s", esp_err_to_name(clear_ret));
        } else {
            ESP_LOGI(TAG, "Consumed one-shot USB MSC boot request");
        }
    }

    nvs_close(handle);
    return requested != 0;
}

void EnterUsbMscMode() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Entering USB Disk Mode");
    ESP_LOGI(TAG, "The SD card will be exposed as USB MSC");
    ESP_LOGI(TAG, "Safely eject on PC, then reset or hold GPIO10 to return");
    ESP_LOGI(TAG, "========================================");

    g_card = InitSdCardForMsc();
    if (g_card == nullptr) {
        ESP_LOGE(TAG, "Cannot enter USB MSC mode without SD card");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    tinyusb_msc_storage_config_t storage_cfg = {
        .medium = {
            .card = g_card,
        },
        .fat_fs = {
            .base_path = nullptr,
            .config = VFS_FAT_MOUNT_DEFAULT_CONFIG(),
            .do_not_format = true,
            .format_flags = 0,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    ESP_ERROR_CHECK(tinyusb_msc_new_storage_sdmmc(&storage_cfg, &g_storage_handle));
    ESP_ERROR_CHECK(tinyusb_msc_set_storage_callback(MscEventCallback, nullptr));

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(UsbEventCallback);
    tusb_cfg.task.size = 4096;
    tusb_cfg.task.priority = 5;
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    ESP_LOGI(TAG, "USB MSC mode active");

    bool exit_button_armed = !IsMscButtonPressed();
    uint32_t pressed_since_ms = 0;
    bool last_attached = g_usb_attached;
    while (true) {
        if (last_attached != g_usb_attached) {
            last_attached = g_usb_attached;
            ESP_LOGI(TAG, "USB host state: %s", last_attached ? "attached" : "detached");
        }

        const bool pressed = IsMscButtonPressed();
        if (!exit_button_armed) {
            if (!pressed) {
                exit_button_armed = true;
                ESP_LOGI(TAG, "MSC exit button armed");
            }
        } else if (pressed) {
            if (pressed_since_ms == 0) {
                pressed_since_ms = static_cast<uint32_t>(esp_log_timestamp());
            } else if (static_cast<uint32_t>(esp_log_timestamp()) - pressed_since_ms >=
                       kMscExitHoldMs) {
                ESP_LOGI(TAG, "GPIO10 long press detected, rebooting to normal mode");
                vTaskDelay(pdMS_TO_TICKS(150));
                esp_restart();
            }
        } else {
            pressed_since_ms = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
