#include "rodakos_adapters/backlight_adapter.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_config.h"
#include "phone_os/phone_system.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "settings.h"

#include <esp_board_manager.h>
#include <dev_display_lcd.h>
#include <dev_lcd_touch.h>
#include <esp_lvgl_port.h>
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

namespace {
constexpr const char* TAG = "RodakOS";
}

extern "C" void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Starting RodakOS with esp-brookesia HAL");

    // Initialize Board Manager (replaces BigSmartBoard::Initialize)
    ESP_ERROR_CHECK(esp_board_manager_init());
    ESP_LOGI(TAG, "Board manager initialized");

    // Get LCD configuration for resolution
    dev_display_lcd_config_t *lcd_cfg = nullptr;
    ESP_ERROR_CHECK(esp_board_manager_get_device_config("display_lcd",
        reinterpret_cast<void**>(&lcd_cfg)));

    Settings display_settings("display", false);
    static PhoneUi ui(lcd_cfg->lcd_width, lcd_cfg->lcd_height);
    ui.SetThemeName(display_settings.GetString("theme", "dark"));

    // Initialize LVGL port with LCD and touch
    void *lcd_handle = nullptr;
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle("display_lcd", &lcd_handle));

    void *touch_handle = nullptr;
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle("lcd_touch", &touch_handle));

    // Cast to device structures to get real handles
    auto lcd_handles = static_cast<dev_display_lcd_handles_t*>(lcd_handle);
    auto touch_handles = static_cast<dev_lcd_touch_handles_t*>(touch_handle);

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_handles->io_handle,
        .panel_handle = lcd_handles->panel_handle,
        .buffer_size = static_cast<uint32_t>(lcd_cfg->lcd_width) * 40,
        .double_buffer = true,
        .hres = lcd_cfg->lcd_width,
        .vres = lcd_cfg->lcd_height,
        .monochrome = false,
        .rotation = {
            .swap_xy = static_cast<bool>(lcd_cfg->swap_xy),
            .mirror_x = static_cast<bool>(lcd_cfg->mirror_x),
            .mirror_y = static_cast<bool>(lcd_cfg->mirror_y),
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,  // 修复 RGB565 字节序
        }
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == nullptr) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return;
    }

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch_handles->touch_handle,
    };
    lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (touch_indev == nullptr) {
        ESP_LOGE(TAG, "Failed to add LVGL touch");
        return;
    }

    ESP_LOGI(TAG, "LVGL port initialized");

    static BacklightAdapter backlight;
    if (!backlight.Initialize()) {
        ESP_LOGE(TAG, "Backlight adapter initialization failed");
        return;
    }

    static WiFiAdapter* wifi = CreateWiFiAdapter();
    if (!wifi->Init()) {
        ESP_LOGE(TAG, "WiFi adapter initialization failed");
        // WiFi 失败不影响系统启动，继续运行
    } else {
        // 尝试自动连接已保存的 WiFi
        WiFiConfig wifi_config;
        std::string saved_ssid, saved_password;
        if (wifi_config.LoadCredentials(saved_ssid, saved_password)) {
            ESP_LOGI(TAG, "Auto-connecting to saved WiFi: %s", saved_ssid.c_str());
            wifi->Connect(saved_ssid, saved_password, [](WiFiStatus status) {
                if (status == WiFiStatus::kConnected) {
                    ESP_LOGI(TAG, "Auto-connect successful");
                } else {
                    ESP_LOGW(TAG, "Auto-connect failed");
                }
            });
        }
    }

    static PhoneServices services;
    services.SetBacklight(&backlight);
    services.SetWiFi(wifi);

    static PhoneSystem system(ui, services);
    if (!system.Start()) {
        ESP_LOGE(TAG, "PhoneSystem start failed");
        return;
    }

    backlight.RestoreBrightness();

    ESP_LOGI(TAG, "RodakOS started successfully");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
