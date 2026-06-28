#include "rodakos_adapters/backlight_adapter.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_config.h"
#include "rodakos_adapters/file_service.h"
#include "phone_os/phone_system.h"
#include "phone_os/phone_services.h"
#include "phone_os/time_service.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "phone_ui/phone_fonts.h"
#include "settings.h"
#include "usb_msc_mode.h"

#include <esp_board_manager.h>
#include <dev_display_lcd.h>
#include <dev_lcd_touch.h>
#include <esp_lvgl_port.h>
#include <esp_err.h>
#include <esp_lcd_touch.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <nvs_flash.h>

namespace {
constexpr const char* TAG = "RodakOS";

struct TouchInputBridge {
    esp_lcd_touch_handle_t handle = nullptr;
    lv_indev_t* indev = nullptr;
    TaskHandle_t task = nullptr;
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    bool running = false;
    bool pressed = false;
    lv_point_t point = {0, 0};
    uint32_t consecutive_errors = 0;
    uint32_t press_log_count = 0;
    uint32_t poll_count = 0;
    uint8_t release_samples = 0;
};

TouchInputBridge g_touch_input;

void TouchReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* touch = static_cast<TouchInputBridge*>(lv_indev_get_driver_data(indev));
    if (touch == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    portENTER_CRITICAL(&touch->lock);
    const bool pressed = touch->pressed;
    const lv_point_t point = touch->point;
    portEXIT_CRITICAL(&touch->lock);

    data->point = point;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void TouchPollTask(void* arg) {
    auto* touch = static_cast<TouchInputBridge*>(arg);
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(20);

    while (touch->running) {
        esp_err_t ret = esp_lcd_touch_read_data(touch->handle);
        bool pressed = false;
        uint8_t point_count = 0;
        uint16_t x = 0;
        uint16_t y = 0;

        if (ret == ESP_OK) {
            esp_lcd_touch_point_data_t point_data[1] = {};
            ret = esp_lcd_touch_get_data(touch->handle, point_data, &point_count, 1);
            if (ret == ESP_OK && point_count > 0) {
                pressed = true;
                x = point_data[0].x;
                y = point_data[0].y;
            }
        }

        bool reported_pressed = false;
        bool changed = false;
        lv_indev_t* indev = nullptr;
        uint32_t error_count = 0;

        portENTER_CRITICAL(&touch->lock);
        indev = touch->indev;
        if (ret == ESP_OK) {
            touch->consecutive_errors = 0;
            if (pressed) {
                touch->release_samples = 0;
                changed = !touch->pressed || touch->point.x != x || touch->point.y != y;
                touch->pressed = true;
                touch->point.x = x;
                touch->point.y = y;
            } else {
                if (touch->pressed && touch->release_samples < 2) {
                    touch->release_samples++;
                }
                if (touch->pressed && touch->release_samples >= 2) {
                    touch->pressed = false;
                    changed = true;
                }
            }
            reported_pressed = touch->pressed;
        } else {
            touch->consecutive_errors++;
            error_count = touch->consecutive_errors;
            if (touch->consecutive_errors >= 3) {
                changed = touch->pressed;
                touch->pressed = false;
                touch->release_samples = 0;
            }
            reported_pressed = touch->pressed;
        }
        portEXIT_CRITICAL(&touch->lock);

        if (ret != ESP_OK && (error_count == 1 || (error_count % 50) == 0)) {
            ESP_LOGW(TAG, "Touch read failed (%s), count=%" PRIu32, esp_err_to_name(ret), error_count);
        }
        if (changed && indev != nullptr) {
            if (reported_pressed) {
                touch->press_log_count++;
                if (touch->press_log_count <= 8 || (touch->press_log_count % 25) == 0) {
                    ESP_LOGI(TAG, "Touch pressed: x=%u y=%u", static_cast<unsigned>(x), static_cast<unsigned>(y));
                }
            } else {
                ESP_LOGI(TAG, "Touch released");
            }
            lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, indev);
        }
        touch->poll_count++;
        if ((touch->poll_count % 750) == 0) {
            ESP_LOGD(TAG, "Touch poll alive: ret=%s points=%u pressed=%d",
                     esp_err_to_name(ret), static_cast<unsigned>(point_count), reported_pressed ? 1 : 0);
        }

        vTaskDelayUntil(&last_wake, period);
    }

    vTaskDelete(nullptr);
}

void InitTouchInput(lv_display_t* disp) {
    void* touch_handle = nullptr;
    esp_err_t ret = esp_board_manager_get_device_handle("lcd_touch", &touch_handle);
    if (ret != ESP_OK || touch_handle == nullptr) {
        ESP_LOGW(TAG, "Touch device not available: %s", esp_err_to_name(ret));
        return;
    }

    auto* touch_handles = static_cast<dev_lcd_touch_handles_t*>(touch_handle);
    if (touch_handles->touch_handle == nullptr) {
        ESP_LOGW(TAG, "Touch driver handle is NULL");
        return;
    }

    g_touch_input.handle = touch_handles->touch_handle;
    g_touch_input.running = true;

    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Failed to lock LVGL for touch input registration");
        g_touch_input.running = false;
        return;
    }

    lv_indev_t* indev = lv_indev_create();
    if (indev != nullptr) {
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, TouchReadCallback);
        lv_indev_set_disp(indev, disp);
        lv_indev_set_driver_data(indev, &g_touch_input);
        g_touch_input.indev = indev;
    }
    lvgl_port_unlock();

    if (indev == nullptr) {
        ESP_LOGW(TAG, "Failed to create LVGL touch input device");
        g_touch_input.running = false;
        return;
    }

#if CONFIG_SOC_CPU_CORES_NUM > 1
    const BaseType_t task_ret = xTaskCreatePinnedToCore(
        TouchPollTask, "touch_poll", 4096, &g_touch_input, 2, &g_touch_input.task, 0);
#else
    const BaseType_t task_ret = xTaskCreate(
        TouchPollTask, "touch_poll", 4096, &g_touch_input, 2, &g_touch_input.task);
#endif
    if (task_ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to start touch polling task");
        g_touch_input.running = false;
        g_touch_input.indev = nullptr;
        return;
    }

    ESP_LOGI(TAG, "Touch input registered with cached polling");
}
}

extern "C" void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    const bool enter_msc_by_flag = ConsumeUsbMscModeBootRequest();
    const bool enter_msc_by_button = CheckMscButtonAtStartup();
    if (enter_msc_by_flag || enter_msc_by_button) {
        ESP_LOGI(TAG, "Entering USB MSC mode before normal RodakOS startup");
        EnterUsbMscMode();
        return;
    }

    ESP_LOGI(TAG, "Starting RodakOS with esp-brookesia HAL");

    // Initialize Board Manager (replaces BigSmartBoard::Initialize)
    ESP_ERROR_CHECK(esp_board_manager_init());
    ESP_LOGI(TAG, "Board manager initialized");

    // Get LCD configuration for resolution
    dev_display_lcd_config_t *lcd_cfg = nullptr;
    ESP_ERROR_CHECK(esp_board_manager_get_device_config("display_lcd",
        reinterpret_cast<void**>(&lcd_cfg)));

    Settings display_settings("display", false);
    const std::string theme_name = display_settings.GetString("theme", "dark");
    rodakos_theme_init_from_name(theme_name.c_str());
    static PhoneUi ui(lcd_cfg->lcd_width, lcd_cfg->lcd_height);
    ui.SetThemeName(rodakos_theme_is_light_name(theme_name.c_str()) ? "light" : "dark");

    // Initialize LVGL adapter with LCD and touch
    void *lcd_handle = nullptr;
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle("display_lcd", &lcd_handle));

    // Cast to device structures to get real handles
    auto lcd_handles = static_cast<dev_display_lcd_handles_t*>(lcd_handle);

    // Initialize LVGL port with LCD.
    // Keep the LVGL task stack in internal RAM. App creation can read NVS from
    // LVGL async callbacks, and ESP-IDF asserts if a flash operation runs while
    // the current task stack lives in PSRAM.
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_priority = 1;  // 低优先级，避免阻塞其他任务
    lvgl_cfg.task_stack = 16384;  // PNG/JPG decoders need more stack than the port default.
    ESP_LOGI(TAG, "LVGL task stack: internal RAM");

#if CONFIG_SOC_CPU_CORES_NUM > 1
    lvgl_cfg.task_affinity = 1;  // 绑定到 CPU1
#endif

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
            .swap_bytes = true,  // RGB565 字节序交换
        }
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == nullptr) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return;
    }

    InitTouchInput(disp);

    ESP_LOGI(TAG, "LVGL port initialized");

    // 初始化字体系统（普惠体中文 + Font Awesome 图标回退），必须在创建界面之前。
    PhoneFontsInit();

    static BacklightAdapter backlight;
    if (!backlight.Initialize()) {
        ESP_LOGE(TAG, "Backlight adapter initialization failed");
        return;
    }

    // 立即打开背光，避免用户看到黑屏
    backlight.RestoreBrightness();
    ESP_LOGI(TAG, "Backlight initialized and turned on");

    static WiFiAdapter* wifi = CreateWiFiAdapter();
    if (!wifi->Init()) {
        ESP_LOGE(TAG, "WiFi adapter initialization failed");
        // WiFi 失败不影响系统启动，继续运行
    }

    static rodakos::FileService* file_service = rodakos::CreateFileService();
    ESP_LOGI(TAG, "File service ready - SD card will mount on demand");

    static PhoneServices services;
    services.SetBacklight(&backlight);
    services.SetWiFi(wifi);
    services.SetFileService(file_service);

    static PhoneSystem system(ui, services);
    if (!system.Start()) {
        ESP_LOGE(TAG, "PhoneSystem start failed");
        return;
    }

    // WiFi 自动连接放在系统启动后，避免阻塞 UI
    if (wifi != nullptr) {
        WiFiConfig wifi_config;
        std::string saved_ssid, saved_password;
        if (wifi_config.LoadCredentials(saved_ssid, saved_password)) {
            ESP_LOGI(TAG, "Auto-connecting to saved WiFi: %s", saved_ssid.c_str());
            wifi->Connect(saved_ssid, saved_password, [](WiFiStatus status) {
                if (status == WiFiStatus::kConnected) {
                    ESP_LOGI(TAG, "Auto-connect successful");
                    TimeServiceStartSavedSync();
                } else {
                    ESP_LOGW(TAG, "Auto-connect failed");
                }
            });
        }
    }

    ESP_LOGI(TAG, "RodakOS started successfully");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
