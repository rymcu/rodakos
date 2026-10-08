#include "rodakos_adapters/backlight_adapter.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_config.h"
#include "rodakos_adapters/file_service.h"
#include "rodakos_adapters/audio_codec_input.h"
#include "rodakos_adapters/qmi8658_motion_sensor.h"
#include "phone_os/phone_system.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/task-retirement.h"
#include "phone_os/phone_services.h"
#include "phone_os/appearance_service.h"
#include "phone_os/touch_pointer_state.h"
#include "phone_os/remote_input_controller.h"
#include "phone_os/audio_focus_service.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"
#include "phone_os/music_player_service.h"
#include "phone_os/recording_service.h"
#include "phone_os/light_service.h"
#include "phone_os/motion_service.h"
#include "phone_os/button_binding_service.h"
#include "phone_os/time_service.h"
#include "phone_os/battery_monitor.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/serial_provisioning_service.h"
#include "phone_os/ota_update_service.h"
#include "phone_os/unified_mqtt_service.h"
#include "phone_os/camera_service.h"
#include "phone_os/webrtc_camera_service.h"
#include "phone_os/display_service.h"
#include "phone_os/webrtc_display_service.h"
#include "phone_os/voice_audio_frontend.h"
#include "phone_os/voice_aec_diagnostic_console.h"
#include "phone_os/voice_assistant_service.h"
#include "phone_os/voice_assistant_transport.h"
#include "phone_os/voice_wake_service.h"
#ifdef RODAKOS_RELEASE_TESTS
#include "phone_os/voice_lifecycle_diagnostic.h"
#include <esp_timer.h>
#endif
#include "phone_os/wake_on_lan_service.h"
#include "phone_os/web_file_system_service.h"
#include "phone_os/realtime_voice_transport.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/remote_text_input.h"
#include "phone_ui/boot_animation.h"
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
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <nvs_flash.h>
#include <cJSON.h>
#include <lvgl.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace {
constexpr const char* TAG = "RodakOS";
// Two RGB565 DMA buffers cost four internal bytes per pixel row. Keep the
// persistent display budget below the wake-audio and authenticated MQTT needs.
constexpr uint32_t kDisplayDrawBufferRows = 24;
using DisplayControlReply = rodakos::UnifiedMqttService::DisplayControlReply;

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
    bool suppress_until_release = false;
    uint8_t suppress_release_samples = 0;
    bool remote_pressed = false;
    lv_point_t remote_point = {0, 0};
    bool remote_active = false;
    bool remote_control_enabled = false;
    bool remote_text_target_available = false;
    lv_obj_t* remote_indicator = nullptr;
    rodakos::TouchPointerState pointer_state;
};

TouchInputBridge g_touch_input;
PhoneNavigation* g_remote_navigation = nullptr;

void WakeRemoteInput() {
    lv_indev_t* indev = nullptr;
    portENTER_CRITICAL(&g_touch_input.lock);
    indev = g_touch_input.indev;
    portEXIT_CRITICAL(&g_touch_input.lock);
    if (indev != nullptr) lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, indev);
}

void ApplyDeferredRemoteAction(void* user_data) {
    std::unique_ptr<std::function<void()>> action(
        static_cast<std::function<void()>*>(user_data));
    (*action)();
}

rodakos::RemoteInputController g_remote_inputs({
    .text = [](const std::string& kind, const std::string& value) {
        const auto result = rodakos::ApplyRemoteTextInput(kind, value);
        return rodakos::RemoteInputResult{result.accepted, result.reason};
    },
    .navigate = [](const std::string& action) {
        return g_remote_navigation != nullptr &&
            (action == "back" ? g_remote_navigation->Back() : g_remote_navigation->ReturnHome());
    },
    .defer_navigation = [](std::function<void()> action) {
        auto pending = std::make_unique<std::function<void()>>(std::move(action));
        if (lv_async_call(ApplyDeferredRemoteAction, pending.get()) != LV_RESULT_OK) return false;
        pending.release();
        return true;
    },
    .wake = WakeRemoteInput,
});

void CompleteSerialLaunch(void*, bool launched) {
    std::fprintf(stdout, "RODAK_APP_LAUNCH_COMPLETE {\"ok\":%s}\n",
                 launched ? "true" : "false");
    std::fflush(stdout);
}

void ResetTouchInputBridge(void* user_data) {
    auto* touch = static_cast<TouchInputBridge*>(user_data);
    if (touch == nullptr) return;
    const bool remote_enabled = g_remote_inputs.IsEnabled();
    portENTER_CRITICAL(&touch->lock);
    const bool had_pressed_input = touch->pressed || touch->remote_pressed;
    touch->pressed = false;
    touch->remote_pressed = false;
    touch->remote_active = remote_enabled;
    touch->remote_control_enabled = remote_enabled;
    touch->remote_text_target_available = false;
    touch->release_samples = 0;
    // Only an actual held contact needs the post-transition release gate.
    touch->suppress_until_release = had_pressed_input;
    touch->suppress_release_samples = 0;
    portEXIT_CRITICAL(&touch->lock);
    g_remote_inputs.ResetForPageTransition();
}

void HandleRemoteControlPayload(const rodakos::StreamLeasePtr& lease,
                                const std::string& payload, DisplayControlReply reply) {
    g_remote_inputs.Handle(lease, payload, std::move(reply));
    const bool enabled = g_remote_inputs.IsEnabled();
    portENTER_CRITICAL(&g_touch_input.lock);
    g_touch_input.remote_control_enabled = enabled;
    g_touch_input.remote_active = enabled;
    if (!enabled) {
        g_touch_input.remote_pressed = false;
        g_touch_input.remote_text_target_available = false;
        if (!g_touch_input.pressed) {
            g_touch_input.suppress_until_release = false;
            g_touch_input.suppress_release_samples = 0;
        }
    }
    portEXIT_CRITICAL(&g_touch_input.lock);
}

void TouchReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* touch = static_cast<TouchInputBridge*>(lv_indev_get_driver_data(indev));
    if (touch == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    g_remote_inputs.ProcessActions();
    lv_obj_t* focused = rodakos::CurrentRemoteTextareaTarget();
    const bool focused_textarea = focused != nullptr && lv_obj_check_type(focused, &lv_textarea_class);
    bool cancelled_remote_gesture = false;
    // The production controller performs the final lease/grant admission around
    // this LVGL input delivery; a cached global enable flag is never authority.
    g_remote_inputs.ReadPointer([&](const rodakos::RemotePointerSample& remote) {
        portENTER_CRITICAL(&touch->lock);
        const bool pressed = touch->pressed;
        const lv_point_t point = touch->point;
        touch->remote_pressed = remote.pressed;
        touch->remote_point = {static_cast<lv_coord_t>(remote.x), static_cast<lv_coord_t>(remote.y)};
        touch->remote_active = remote.enabled;
        touch->remote_control_enabled = remote.enabled;
        touch->remote_text_target_available = remote.enabled && focused_textarea;
        portEXIT_CRITICAL(&touch->lock);
        if (touch->remote_indicator != nullptr) {
            if (remote.enabled) lv_obj_clear_flag(touch->remote_indicator, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(touch->remote_indicator, LV_OBJ_FLAG_HIDDEN);
        }
        const lv_point_t remote_point{static_cast<lv_coord_t>(remote.x), static_cast<lv_coord_t>(remote.y)};
        if (touch->pointer_state.Read(pressed, point, remote.pressed, remote_point, *data,
                                      remote.cancel_generation)) {
            lv_indev_reset(indev, nullptr);
            cancelled_remote_gesture = true;
        }
    });
    // A cancellation releases prev_state before an already queued replacement
    // press is read; ordinary down/move/up still get separate input cycles.
    data->continue_reading = cancelled_remote_gesture;
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
        bool local_pressed_now = false;
        lv_indev_t* indev = nullptr;
        uint32_t error_count = 0;

        portENTER_CRITICAL(&touch->lock);
        indev = touch->indev;
        if (ret == ESP_OK) {
            touch->consecutive_errors = 0;
            if (touch->suppress_until_release) {
                touch->pressed = false;
                touch->release_samples = 0;
                if (pressed) {
                    local_pressed_now = true;
                    touch->remote_pressed = false;
                    touch->suppress_release_samples = 0;
                } else {
                    if (touch->suppress_release_samples < 2) {
                        touch->suppress_release_samples++;
                    }
                    if (touch->suppress_release_samples >= 2) {
                        touch->suppress_until_release = false;
                    }
                }
            } else if (pressed) {
                local_pressed_now = true;
                touch->remote_pressed = false;
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
                touch->suppress_until_release = false;
                touch->suppress_release_samples = 0;
            }
            reported_pressed = touch->pressed;
        }
        portEXIT_CRITICAL(&touch->lock);
        if (local_pressed_now) {
            g_remote_inputs.OnLocalTouch();
        }

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
            ESP_LOGI(TAG, "Touch poll alive: ret=%s points=%u pressed=%d errors=%" PRIu32,
                     esp_err_to_name(ret), static_cast<unsigned>(point_count),
                     reported_pressed ? 1 : 0, error_count);
        }

        vTaskDelayUntil(&last_wake, period);
    }

    vTaskDelete(nullptr);
}

lv_indev_t* InitTouchInput(lv_display_t* disp) {
    void* touch_handle = nullptr;
    esp_err_t ret = esp_board_manager_get_device_handle("lcd_touch", &touch_handle);
    if (ret != ESP_OK || touch_handle == nullptr) {
        ESP_LOGW(TAG, "Touch device not available: %s", esp_err_to_name(ret));
        return nullptr;
    }

    auto* touch_handles = static_cast<dev_lcd_touch_handles_t*>(touch_handle);
    if (touch_handles->touch_handle == nullptr) {
        ESP_LOGW(TAG, "Touch driver handle is NULL");
        return nullptr;
    }

    g_touch_input.handle = touch_handles->touch_handle;
    g_touch_input.running = true;

    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "Failed to lock LVGL for touch input registration");
        g_touch_input.running = false;
        return nullptr;
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
        return nullptr;
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
        return nullptr;
    }

    ESP_LOGI(TAG, "Touch input registered with cached polling");
    return indev;
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

    ESP_LOGI(TAG, "Starting RodakOS with Board Manager HAL");

    // Initialize Board Manager (replaces BigSmartBoard::Initialize)
    ESP_ERROR_CHECK(esp_board_manager_init());
    ESP_LOGI(TAG, "Board manager initialized");

    static rodakos::FileService* file_service = rodakos::CreateFileService();
    static rodakos::DeviceCloudConfigService device_cloud_config_service;
    static rodakos::AppearanceService appearance_service(device_cloud_config_service, file_service);
    appearance_service.BeginBootLoad();

    // Get LCD configuration for resolution
    dev_display_lcd_config_t *lcd_cfg = nullptr;
    ESP_ERROR_CHECK(esp_board_manager_get_device_config("display_lcd",
        reinterpret_cast<void**>(&lcd_cfg)));

    Settings display_settings("display", false);
    const std::string theme_name = display_settings.GetString("theme", "dark");
    rodakos_theme_init_from_name(theme_name.c_str());
    static PhoneUi ui(lcd_cfg->lcd_width, lcd_cfg->lcd_height);
    ui.SetThemeName(theme_name);

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
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(lcd_cfg->lcd_width) * kDisplayDrawBufferRows,
        .double_buffer = true,
        .trans_size = 0,
        .hres = lcd_cfg->lcd_width,
        .vres = lcd_cfg->lcd_height,
        .monochrome = false,
        .rotation = {
            .swap_xy = static_cast<bool>(lcd_cfg->swap_xy),
            .mirror_x = static_cast<bool>(lcd_cfg->mirror_x),
            .mirror_y = static_cast<bool>(lcd_cfg->mirror_y),
        },
        .rounder_cb = nullptr,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
            .swap_bytes = true,  // RGB565 字节序交换
            .full_refresh = false,
            .direct_mode = false,
        },
    };
    ESP_LOGI(TAG, "Display DMA draw buffers: rows=%u buffers=2 bytes=%u",
             static_cast<unsigned>(kDisplayDrawBufferRows),
             static_cast<unsigned>(disp_cfg.buffer_size * sizeof(uint16_t) * 2));
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == nullptr) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return;
    }

    lv_indev_t* touch_indev = InitTouchInput(disp);
    ui.SetPrimaryInput(touch_indev);
    ui.SetInputResetCallback(ResetTouchInputBridge, &g_touch_input);
    ui.SetPhysicalInputCallback([](void* data) {
        (void)data;
        return !g_remote_inputs.IsEnabled();
    }, &g_touch_input);

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

    auto boot_assets = appearance_service.WaitBootAssets();
    std::string local_theme;
    uint32_t local_primary = 0;
    if (boot_assets) {
        rodakos_theme_apply_preset_primary(boot_assets->metadata.theme_preset.c_str(),
                                          boot_assets->metadata.theme_primary);
        ui.SyncThemeName(boot_assets->metadata.theme_preset);
        if (const auto* wallpaper = boot_assets->metadata.FindResource(
                boot_assets->metadata.wallpaper_resource_id); wallpaper != nullptr) {
            ui.SetWallpaper(boot_assets->WallpaperBuffer(), wallpaper->width, wallpaper->height);
        }
    }
    if (appearance_service.GetLocalTheme(local_theme, local_primary)) {
        rodakos_theme_apply_preset_primary(local_theme.c_str(), local_primary);
        ui.SyncThemeName(local_theme);
    }
    BootAnimation boot_animation(ui);
    bool appearance_confirm_pending = boot_assets != nullptr;
    bool animation_started = boot_animation.Start(boot_assets);
    if (!animation_started && boot_assets) {
        appearance_service.RejectBootCandidate("animation_surface_unavailable");
        appearance_confirm_pending = false;
        ui.SetWallpaper({}, 0, 0);
        ui.SetThemeName(theme_name);
        animation_started = boot_animation.Start();
    }
    boot_assets.reset();

    static WiFiAdapter* wifi = CreateWiFiAdapter();
    if (!wifi->Init()) {
        ESP_LOGE(TAG, "WiFi adapter initialization failed");
        // WiFi 失败不影响系统启动，继续运行
    }

    ESP_LOGI(TAG, "File service ready - SD card will mount on demand");

    static rodakos::AudioOutputService audio_output_service;
    static rodakos::AudioService audio_service(audio_output_service);
    static rodakos::MusicPlayerService music_player_service(audio_service, file_service);
    static rodakos::LightService light_service;
    light_service.Init();
    static rodakos::Qmi8658MotionSensor qmi8658_motion_sensor;
    static rodakos::MotionService motion_service(&qmi8658_motion_sensor);
    static rodakos::ButtonBindingService button_binding_service;
    static rodakos::AudioFocusService audio_focus_service(
        music_player_service, audio_output_service);
    static rodakos::AudioCodecInput audio_input;
    static rodakos::RecordingService recording_service(audio_input, file_service, &audio_focus_service);
    static rodakos::OtaUpdateService ota_update_service(
        device_cloud_config_service, file_service);
    static rodakos::BatteryMonitor battery_monitor;
    static rodakos::UnifiedMqttService unified_mqtt_service(
        device_cloud_config_service, ota_update_service, &audio_output_service,
        &battery_monitor, &light_service);
    unified_mqtt_service.SetAppearanceService(&appearance_service);
    static rodakos::RodakRealtimeVoiceTransport voice_assistant_transport(
        device_cloud_config_service);
    static rodakos::VoiceAudioFrontend voice_audio_frontend(audio_input);
    static rodakos::VoiceAssistantService voice_assistant_service(
        audio_focus_service,
        voice_assistant_transport,
        voice_audio_frontend,
        audio_output_service);
    static rodakos::VoiceWakeService voice_wake_service(
        voice_assistant_service, voice_audio_frontend);
    unified_mqtt_service.SetVoiceWakeService(&voice_wake_service);
    static rodakos::SerialProvisioningService serial_provisioning_service(
        wifi, device_cloud_config_service,
        []() { unified_mqtt_service.RequestCredentialRefresh(); },
        [](const std::string& command) {
            if (command.rfind("aec_", 0) == 0) {
                return rodakos::HandleVoiceAecDiagnosticCommand(voice_audio_frontend, command);
            }
            if (command == "audio_replay") {
                return voice_audio_frontend.ReplayDiagnosticAudio();
            }
            if (command == "audio_live") {
                voice_audio_frontend.ClearDiagnosticAudio();
                ESP_LOGI(TAG, "USB diagnostic injection disabled; using physical microphones");
                return true;
            }
            if (command == "wake") {
                return voice_audio_frontend.QueueDiagnosticCommand([]() {
                    if (voice_assistant_service.GetPhaseSnapshot() ==
                            rodakos::VoiceAssistantPhase::kSpeaking ||
                        voice_audio_frontend.ArmDiagnosticAudio()) {
                        voice_wake_service.NotifyWakeWordDetected("USB simulated wake");
                    }
                });
            }
            if (command == "stop") {
                return voice_audio_frontend.QueueDiagnosticCommand([]() {
                    voice_assistant_service.StopInteraction();
                    voice_audio_frontend.ClearDiagnosticAudio();
                });
            }
            return voice_audio_frontend.LoadDiagnosticAudio(command);
        });
    static rodakos::WakeOnLanService wake_on_lan_service(wifi);
    if (!rodakos::SerialProvisioningService::RecoverPendingTransaction(
            device_cloud_config_service)) {
        ESP_LOGW(TAG, "Pending serial provisioning recovery did not complete");
    }
    ESP_LOGI(TAG, "Audio services ready - focus, assistant, and playback open codec on demand");
    ESP_LOGI(TAG, "Recording service ready - audio ADC opens on demand");

    static rodakos::WebFileSystemService web_files_service(file_service);
    ESP_LOGI(TAG, "Web file system ready - start from Settings when needed");

    static rodakos::CameraService camera_service(file_service);
    ESP_LOGI(TAG, "Camera service ready - camera opens on demand");
    static rodakos::WebRtcCameraService web_rtc_camera_service(&camera_service);
    unified_mqtt_service.SetWebRtcCameraService(&web_rtc_camera_service);
    ESP_LOGI(TAG, "WebRTC camera service ready - starts on MQTT camera.stream.start");
    static rodakos::DisplayService display_service(disp);
    if (lvgl_port_lock(1000)) {
        if (!display_service.Attach()) {
            ESP_LOGW(TAG, "Display capture listener unavailable");
        }
        lvgl_port_unlock();
    }
    static rodakos::WebRtcDisplayService web_rtc_display_service(&display_service);
    unified_mqtt_service.SetWebRtcDisplayService(&web_rtc_display_service);
    appearance_service.SetBusyGate([]() {
        const auto voice = voice_assistant_service.GetState();
        return ota_update_service.IsBusy() || voice.focus_active || voice.transport_active ||
               voice.recorder_active || web_rtc_display_service.IsRunning() ||
               web_rtc_camera_service.IsRunning();
    });
    ESP_LOGI(TAG, "WebRTC display service ready - starts on MQTT display.stream.start");

#ifdef RODAKOS_RELEASE_TESTS
    static rodakos::VoiceLifecycleDiagnostic voice_lifecycle_diagnostic({
        .busy_reason = [](void*) -> const char* {
            if (ota_update_service.IsBusy()) return "ota_busy";
            if (camera_service.GetState().preview_running || web_rtc_camera_service.IsRunning())
                return "camera_busy";
            if (web_rtc_display_service.IsRunning()) return "display_busy";
            const auto recording = recording_service.GetState().status;
            if (recording == rodakos::RecordingStatus::kStarting ||
                recording == rodakos::RecordingStatus::kRecording ||
                recording == rodakos::RecordingStatus::kStopping) return "recording_busy";
            if (audio_service.IsBusy()) return "music_busy";
            const auto focus = audio_focus_service.GetState();
            if (focus.active && focus.owner != "voice-assistant") return "audio_focus_busy";
            // An active assistant session is the intended three-task test target.
            return nullptr;
        },
        .snapshot = [](void*) {
            rodakos::VoiceLifecycleSnapshot snapshot;
            snapshot.tasks.assistant = xTaskGetHandle("assistant_io") != nullptr;
            snapshot.tasks.capture = xTaskGetHandle("voice_frontend") != nullptr;
            snapshot.tasks.supervisor = xTaskGetHandle("voice_wake") != nullptr;
            const auto assistant = voice_assistant_service.GetState();
            snapshot.assistant_stopping = assistant.stopping;
            using DiagnosticPhase = rodakos::VoiceLifecycleAssistantPhase;
            switch (assistant.phase) {
                case rodakos::VoiceAssistantPhase::kIdle: snapshot.assistant_phase = DiagnosticPhase::kIdle; break;
                case rodakos::VoiceAssistantPhase::kConnecting: snapshot.assistant_phase = DiagnosticPhase::kConnecting; break;
                case rodakos::VoiceAssistantPhase::kListening: snapshot.assistant_phase = DiagnosticPhase::kListening; break;
                case rodakos::VoiceAssistantPhase::kSpeaking: snapshot.assistant_phase = DiagnosticPhase::kSpeaking; break;
                case rodakos::VoiceAssistantPhase::kError: snapshot.assistant_phase = DiagnosticPhase::kError; break;
            }
            snapshot.uptime_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
            snapshot.internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            snapshot.internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            snapshot.internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            snapshot.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            snapshot.psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            return snapshot;
        },
        .wake_state = [](void*) {
            const auto state = voice_wake_service.GetState();
            return rodakos::VoiceLifecycleWakeState{state.enabled, state.listening};
        },
        .deinit = [](void*) { voice_wake_service.Deinit(); },
        .restart = [](void*) { return voice_wake_service.Start(); },
        .emit = rodakos::PrintVoiceLifecycleEvent
    });
    serial_provisioning_service.SetVoiceLifecycleDiagnostic(&voice_lifecycle_diagnostic);
    ESP_LOGW(TAG, "RODAKOS_RELEASE_TESTS active: USB voice lifecycle diagnostic enabled");
#endif

    static PhoneServices services;
    services.SetAppearance(&appearance_service);
    services.SetBacklight(&backlight);
    services.SetWiFi(wifi);
    services.SetBattery(&battery_monitor);
    services.SetFileService(file_service);
    services.SetAudio(&audio_service);
    services.SetAudioOutput(&audio_output_service);
    services.SetMusicPlayer(&music_player_service);
    services.SetRecording(&recording_service);
    services.SetLights(&light_service);
    services.SetMotion(&motion_service);
    services.SetButtons(&button_binding_service);
    services.SetAudioFocus(&audio_focus_service);
    services.SetDeviceCloud(&device_cloud_config_service);
    services.SetSerialProvisioning(&serial_provisioning_service);
    services.SetVoiceAssistant(&voice_assistant_service);
    services.SetVoiceWake(&voice_wake_service);
    services.SetWakeOnLan(&wake_on_lan_service);
    services.SetWebFiles(&web_files_service);
    services.SetCamera(&camera_service);
    services.SetDeviceCloudUnboundCallback([]() {
        appearance_service.ForgetPublisher();
        unified_mqtt_service.StopWebRtcCameraStream();
        unified_mqtt_service.StopWebRtcDisplayStream();
        voice_assistant_service.StopInteraction();
        unified_mqtt_service.Stop();
    });
    services.SetDeviceCloudBoundCallback([]() {
        unified_mqtt_service.ReconnectAfterCredentialChange();
    });

    static PhoneSystem system(ui, services);
    if (!system.Start()) {
        ESP_LOGE(TAG, "PhoneSystem start failed");
        boot_animation.Stop();
        return;
    }
    serial_provisioning_service.SetAppLaunchCallback([](const std::string& requested_id) {
        return system.navigation().RequestLaunch(requested_id, CompleteSerialLaunch);
    });

    g_remote_navigation = &system.navigation();
    unified_mqtt_service.SetWebRtcDisplayControlCallback(HandleRemoteControlPayload);
    if (lvgl_port_lock(1000)) {
        g_touch_input.remote_indicator = lv_label_create(lv_layer_top());
        lv_label_set_text(g_touch_input.remote_indicator, "远程控制中");
        // 默认 LVGL 字体只覆盖拉丁字符；使用组合字体渲染中文状态，避免显示五个缺字方框。
        lv_obj_set_style_text_font(g_touch_input.remote_indicator, &phone_font_14, 0);
        lv_obj_set_style_text_color(g_touch_input.remote_indicator, lv_color_hex(0xff4d4f), 0);
        lv_obj_align(g_touch_input.remote_indicator, LV_ALIGN_TOP_MID, 0, 2);
        lv_obj_add_flag(g_touch_input.remote_indicator, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }

    boot_animation.Finish();

    button_binding_service.Init(system.navigation(), ui);

    if (!serial_provisioning_service.Start()) {
        ESP_LOGW(TAG, "Serial provisioning service failed to start");
    }

    // Reserve the internal MQTT worker stack before the wake model fragments SRAM.
    // WiFi auto-connect remains below local OTA confirmation.
    const bool mqtt_started = unified_mqtt_service.Start();
    if (!mqtt_started) {
        ESP_LOGW(TAG, "Unified MQTT service failed to start");
    }

    WiFiConfig boot_wifi_config;
    std::string boot_ssid;
    std::string boot_password;
    const bool has_saved_wifi = boot_wifi_config.LoadCredentials(boot_ssid, boot_password);
    const bool voice_wake_started = has_saved_wifi && voice_wake_service.Start();
    const auto voice_wake_state = has_saved_wifi ? voice_wake_service.GetState()
                                                  : rodakos::VoiceWakeState{};
    if (!has_saved_wifi) {
        ESP_LOGI(TAG, "Voice wake deferred until serial WiFi provisioning completes");
    } else if (!voice_wake_started) {
        ESP_LOGW(TAG, "Voice wake service failed to start: %s",
                 voice_wake_state.message.c_str());
    } else {
        ESP_LOGI(TAG, "Voice wake service %s: %s",
                 voice_wake_state.enabled ? "enabled" : "disabled",
                 voice_wake_state.message.c_str());
    }
    // 到达此处即通过本地启动健康门槛；先持久化，再允许 MQTT connected 回调上报。
    if (!ota_update_service.ConfirmRunningImage()) {
        ESP_LOGW(TAG, "Local boot confirmation did not complete");
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

    uint32_t appearance_retry_ticks = 0;
    uint32_t main_health_ticks = 0;
    while (true) {
#ifdef RODAKOS_RELEASE_TESTS
        // Outside all service/UI/serial locks, on the permanent internal-stack task.
        voice_lifecycle_diagnostic.Pump();
#endif
        rodakos::PumpTaskRetirements();
        if (animation_started && boot_animation.HasCompleted()) {
            appearance_service.RecordAnimationMs(boot_animation.duration_ms());
            appearance_service.ReleaseBootAssets();
            animation_started = false;
        }
        if (!animation_started && appearance_confirm_pending &&
            appearance_service.ConfirmBootHealthy()) {
            appearance_confirm_pending = false;
        }
        if (++appearance_retry_ticks >= 5) {
            appearance_retry_ticks = 0;
            if (unified_mqtt_service.IsConnected()) appearance_service.OnNetworkReady();
        }
        if (++main_health_ticks >= 30) {
            main_health_ticks = 0;
            ESP_LOGI(TAG, "Main health: stack_min_free=%u internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u",
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
