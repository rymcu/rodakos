#include "rodakos_adapters/backlight_adapter.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_config.h"
#include "rodakos_adapters/file_service.h"
#include "rodakos_adapters/audio_codec_input.h"
#include "rodakos_adapters/qmi8658_motion_sensor.h"
#include "phone_os/phone_system.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/appearance_service.h"
#include "phone_os/touch_pointer_state.h"
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
std::mutex g_remote_action_mutex;
struct RemotePointerEvent {
    bool pressed = false;
    // move 可合并；down/up 是必须保序的边界。
    bool move = false;
    lv_point_t point = {0, 0};
    DisplayControlReply reply;
};
struct RemoteAction {
    std::string kind;
    std::string value;
    DisplayControlReply reply;
};
struct DeferredRemoteNavigation {
    PhoneNavigation* navigation = nullptr;
    std::string action;
    DisplayControlReply reply;
};
std::deque<RemoteAction> g_remote_actions;
std::deque<RemotePointerEvent> g_remote_pointer_events;
PhoneNavigation* g_remote_navigation = nullptr;
bool g_remote_navigation_pending = false;

void ApplyDeferredRemoteNavigation(void* user_data) {
    std::unique_ptr<DeferredRemoteNavigation> request(
        static_cast<DeferredRemoteNavigation*>(user_data));
    bool remote_enabled = false;
    portENTER_CRITICAL(&g_touch_input.lock);
    remote_enabled = g_touch_input.remote_control_enabled;
    portEXIT_CRITICAL(&g_touch_input.lock);
    const bool ok = remote_enabled && request->navigation != nullptr &&
                    (request->action == "back" ? request->navigation->Back()
                                                : request->navigation->ReturnHome());
    {
        std::lock_guard<std::mutex> lock(g_remote_action_mutex);
        g_remote_navigation_pending = false;
    }
    if (request->reply) request->reply(ok, ok ? nullptr : "navigation_rejected");
    if (g_touch_input.indev != nullptr) {
        lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, g_touch_input.indev);
    }
}

bool IsValidUtf8(const char* text) {
    if (text == nullptr) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p != 0) {
        uint32_t codepoint = 0;
        size_t length = 0;
        if (*p < 0x80) { codepoint = *p; length = 1; }
        else if (*p >= 0xc2 && *p <= 0xdf) { codepoint = *p & 0x1f; length = 2; }
        else if (*p >= 0xe0 && *p <= 0xef) { codepoint = *p & 0x0f; length = 3; }
        else if (*p >= 0xf0 && *p <= 0xf4) { codepoint = *p & 0x07; length = 4; }
        else return false;
        for (size_t i = 1; i < length; ++i) {
            if ((p[i] & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (p[i] & 0x3f);
        }
        if ((length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        p += length;
    }
    return true;
}

struct SerialLaunchRequest {
    PhoneNavigation* navigation = nullptr;
    std::string app_id;
};

void LaunchAppFromSerial(void* user_data) {
    std::unique_ptr<SerialLaunchRequest> request(
        static_cast<SerialLaunchRequest*>(user_data));
    const bool launched = request != nullptr && request->navigation != nullptr &&
                          request->navigation->Launch(request->app_id);
    std::fprintf(stdout, "RODAK_APP_LAUNCH_COMPLETE {\"ok\":%s}\n",
                 launched ? "true" : "false");
    std::fflush(stdout);
}

void ResetTouchInputBridge(void* user_data) {
    auto* touch = static_cast<TouchInputBridge*>(user_data);
    if (touch == nullptr) {
        return;
    }

    portENTER_CRITICAL(&touch->lock);
    // App transitions call PhoneUi::ResetInputState() before and after
    // replacing the current page. Release any held pointer state, but keep
    // the explicit remote-control lease alive for the screen session.
    const bool remote_enabled = touch->remote_control_enabled;
    const bool had_pressed_input = touch->pressed || touch->remote_pressed;
    touch->pressed = false;
    touch->remote_pressed = false;
    touch->remote_active = remote_enabled;
    touch->remote_text_target_available = false;
    touch->release_samples = 0;
    // Only suppress a post-transition release when an input was actually
    // held at reset time.  Unconditionally arming this gate made every page
    // transition discard the next local tap (remote pointer up is already a
    // release), which left the physical touch input apparently dead after a
    // remote-control navigation.
    touch->suppress_until_release = had_pressed_input;
    touch->suppress_release_samples = 0;
    portEXIT_CRITICAL(&touch->lock);
    {
        std::lock_guard<std::mutex> lock(g_remote_action_mutex);
        // 远程导航替换应用时可能同步重置输入桥。保留导航期间到达的
        // 后续可靠动作和 pointer 边界，使快速 Home -> swipe 序列在切换
        // 完成后继续执行；本地切换仍按原逻辑丢弃过期动作。
        if (!g_remote_navigation_pending) {
            g_remote_actions.clear();
            g_remote_pointer_events.clear();
        }
    }
}

void ApplyRemoteActionsOnLvglThread() {
    while (true) {
        RemoteAction action;
        {
            std::lock_guard<std::mutex> lock(g_remote_action_mutex);
            if (g_remote_navigation_pending || g_remote_actions.empty()) return;
            // pointer 与 shortcut/text 使用独立队列，但必须保持同一条远程
            // 输入时序。当前 TouchReadCallback 会在本函数返回后消费一个
            // pointer 事件，因此只要仍有 pointer 待处理，就让它先进入
            // LVGL，再在下一轮处理可靠动作。这样 pointer up 后紧随的
            // home/back 不会先触发导航并在 ResetTouchInputBridge() 中清掉
            // 尚未确认的 pointer ACK；动作之后到达的 pointer 也不会越过
            // 已经排队的导航。
            if (!g_remote_pointer_events.empty()) return;
            action = std::move(g_remote_actions.front());
            g_remote_actions.pop_front();
        }
        const auto& kind = action.kind;
        const auto& value = action.value;
        if (kind == "shortcut" && (value == "back" || value == "home")) {
            {
                std::lock_guard<std::mutex> lock(g_remote_action_mutex);
                g_remote_navigation_pending = true;
            }
            auto* request = new DeferredRemoteNavigation{
                .navigation = g_remote_navigation,
                .action = value,
                .reply = action.reply,
            };
            if (lv_async_call(ApplyDeferredRemoteNavigation, request) != LV_RESULT_OK) {
                delete request;
                std::lock_guard<std::mutex> lock(g_remote_action_mutex);
                g_remote_navigation_pending = false;
                if (action.reply) action.reply(false, "navigation_queue_full");
            }
            return;
        }
        const auto result = rodakos::ApplyRemoteTextInput(kind, value);
        if (action.reply) action.reply(result.accepted, result.reason);
    }
}

void HandleRemoteControlPayload(const std::string& payload,
                                DisplayControlReply reply) {
    if (payload.empty()) {
        lv_indev_t* indev = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_remote_action_mutex);
            g_remote_actions.clear();
            g_remote_pointer_events.clear();
            g_remote_navigation_pending = false;
            portENTER_CRITICAL(&g_touch_input.lock);
            g_touch_input.remote_pressed = false;
            g_touch_input.remote_active = false;
            g_touch_input.remote_control_enabled = false;
            g_touch_input.remote_text_target_available = false;
            // A stream teardown is also a control-lease teardown.  If no
            // local contact is currently cached, there is no reason to keep
            // a stale transition suppression gate armed; clear it before
            // waking LVGL so the next physical tap is delivered immediately.
            if (!g_touch_input.pressed) {
                g_touch_input.suppress_until_release = false;
                g_touch_input.suppress_release_samples = 0;
            }
            indev = g_touch_input.indev;
            portEXIT_CRITICAL(&g_touch_input.lock);
        }
        if (indev != nullptr) lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, indev);
        if (reply) reply(true, nullptr);
        return;
    }
    cJSON* root = cJSON_ParseWithLength(payload.data(), payload.size());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        if (reply) reply(false, "invalid_json");
        return;
    }
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON* kind = cJSON_GetObjectItemCaseSensitive(root, "kind");
    bool accepted = cJSON_IsNumber(version) && version->valueint == 1 && cJSON_IsString(kind);
    if (accepted && std::strcmp(kind->valuestring, "control") == 0) {
        const cJSON* action = cJSON_GetObjectItemCaseSensitive(root, "action");
        accepted = cJSON_IsString(action) &&
                   (std::strcmp(action->valuestring, "enable") == 0 ||
                    std::strcmp(action->valuestring, "disable") == 0);
        if (accepted) {
            const bool enabled = std::strcmp(action->valuestring, "enable") == 0;
            portENTER_CRITICAL(&g_touch_input.lock);
            g_touch_input.remote_control_enabled = enabled;
            g_touch_input.remote_active = enabled;
            if (!enabled) g_touch_input.remote_pressed = false;
            if (!enabled && !g_touch_input.pressed) {
                g_touch_input.suppress_until_release = false;
                g_touch_input.suppress_release_samples = 0;
            }
            portEXIT_CRITICAL(&g_touch_input.lock);
            if (!enabled) {
                std::lock_guard<std::mutex> lock(g_remote_action_mutex);
                g_remote_actions.clear();
                g_remote_pointer_events.clear();
                g_remote_navigation_pending = false;
            }
            if (reply) reply(true, nullptr);
        }
    } else if (accepted && std::strcmp(kind->valuestring, "pointer") == 0) {
        const cJSON* action = cJSON_GetObjectItemCaseSensitive(root, "action");
        const cJSON* x = cJSON_GetObjectItemCaseSensitive(root, "x");
        const cJSON* y = cJSON_GetObjectItemCaseSensitive(root, "y");
        accepted = cJSON_IsString(action) && cJSON_IsNumber(x) && cJSON_IsNumber(y) &&
                   x->valuedouble >= 0.0 && x->valuedouble < 320.0 &&
                   y->valuedouble >= 0.0 && y->valuedouble < 240.0 &&
                   x->valuedouble == static_cast<double>(x->valueint) &&
                   y->valuedouble == static_cast<double>(y->valueint);
        const bool is_down = accepted && std::strcmp(action->valuestring, "down") == 0;
        const bool is_up = accepted && std::strcmp(action->valuestring, "up") == 0;
        const bool is_move = accepted && std::strcmp(action->valuestring, "move") == 0;
        accepted = accepted && (is_down || is_up || is_move);
        bool enabled = false;
        if (accepted) {
            portENTER_CRITICAL(&g_touch_input.lock);
            enabled = g_touch_input.remote_control_enabled;
            portEXIT_CRITICAL(&g_touch_input.lock);
        }
        accepted = accepted && enabled;
        if (accepted) {
            RemotePointerEvent event;
            event.pressed = !is_up;
            event.move = is_move;
            event.point.x = static_cast<lv_coord_t>(x->valueint);
            event.point.y = static_cast<lv_coord_t>(y->valueint);
            event.reply = std::move(reply);
            std::lock_guard<std::mutex> lock(g_remote_action_mutex);
            if (is_move) {
                // 只保留连续 move 的最新事件；down/up 是保序边界，不能跨越合并。
                if (!g_remote_pointer_events.empty() &&
                    g_remote_pointer_events.back().move) {
                    auto coalesced_reply = std::move(g_remote_pointer_events.back().reply);
                    g_remote_pointer_events.back() = std::move(event);
                    if (coalesced_reply) coalesced_reply(true, "coalesced");
                } else if (g_remote_pointer_events.size() < 32) {
                    g_remote_pointer_events.push_back(std::move(event));
                } else {
                    // 队列中全是 down/up 时无法安全挤出边界；move 可丢弃，
                    // 但仍确认已接受，避免发送端等待一个永远不会执行的旧坐标。
                    accepted = true;
                    if (event.reply) event.reply(true, "coalesced");
                    reply = {};
                }
            } else {
                // 队列满时优先淘汰旧 move，为可靠的 down/up 腾出空间，
                // 同时保留所有 down/up 的顺序边界。
                while (g_remote_pointer_events.size() >= 32) {
                    auto move_it = std::find_if(
                        g_remote_pointer_events.begin(), g_remote_pointer_events.end(),
                        [](const RemotePointerEvent& queued) { return queued.move; });
                    if (move_it == g_remote_pointer_events.end()) break;
                    g_remote_pointer_events.erase(move_it);
                }
                if (g_remote_pointer_events.size() >= 32) {
                    accepted = false;
                } else {
                    g_remote_pointer_events.push_back(std::move(event));
                }
            }
        }
    } else if (accepted && std::strcmp(kind->valuestring, "text") == 0) {
        const cJSON* text = cJSON_GetObjectItemCaseSensitive(root, "text");
        bool enabled = false;
        portENTER_CRITICAL(&g_touch_input.lock);
        enabled = g_touch_input.remote_control_enabled;
        portEXIT_CRITICAL(&g_touch_input.lock);
        accepted = cJSON_IsString(text) && std::strlen(text->valuestring) <= 1024 &&
                   IsValidUtf8(text->valuestring) && enabled;
        if (!accepted && reply) {
            if (!cJSON_IsString(text) || std::strlen(text->valuestring) > 1024) reply(false, "invalid_text");
            else if (!IsValidUtf8(text->valuestring)) reply(false, "invalid_utf8");
            else if (!enabled) reply(false, "control_disabled_or_invalid");
            reply = {};
        }
        if (accepted) {
            std::lock_guard<std::mutex> lock(g_remote_action_mutex);
            if (g_remote_actions.size() >= 16) accepted = false;
            else g_remote_actions.push_back(RemoteAction{"text", text->valuestring, std::move(reply)});
        }
    } else if (accepted && std::strcmp(kind->valuestring, "shortcut") == 0) {
        const cJSON* shortcut = cJSON_GetObjectItemCaseSensitive(root, "shortcut");
        bool enabled = false;
        portENTER_CRITICAL(&g_touch_input.lock);
        enabled = g_touch_input.remote_control_enabled;
        portEXIT_CRITICAL(&g_touch_input.lock);
        accepted = cJSON_IsString(shortcut) && enabled;
        if (accepted) {
            const std::string value = shortcut->valuestring;
            accepted = value == "back" || value == "home" || value == "enter" ||
                       value == "escape" || value == "backspace" || value == "delete";
            if (accepted) {
                std::lock_guard<std::mutex> lock(g_remote_action_mutex);
                if (g_remote_actions.size() >= 16) accepted = false;
                else g_remote_actions.push_back(RemoteAction{"shortcut", value, std::move(reply)});
            }
        }
    } else {
        accepted = false;
    }
    cJSON_Delete(root);
    if (g_touch_input.indev != nullptr) lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, g_touch_input.indev);
    if (!accepted && reply) reply(false, "control_disabled_or_invalid");
}
void TouchReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* touch = static_cast<TouchInputBridge*>(lv_indev_get_driver_data(indev));
    if (touch == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    ApplyRemoteActionsOnLvglThread();
    RemotePointerEvent pointer_event;
    bool has_pointer_event = false;
    bool navigation_pending = false;
    {
        std::lock_guard<std::mutex> lock(g_remote_action_mutex);
        navigation_pending = g_remote_navigation_pending;
        if (!navigation_pending && !g_remote_pointer_events.empty()) {
            pointer_event = g_remote_pointer_events.front();
            g_remote_pointer_events.pop_front();
            has_pointer_event = true;
        }
    }

    lv_obj_t* focused = rodakos::CurrentRemoteTextareaTarget();
    const bool focused_textarea = focused != nullptr && lv_obj_check_type(focused, &lv_textarea_class);

    portENTER_CRITICAL(&touch->lock);
    if (has_pointer_event && touch->remote_control_enabled) {
        touch->remote_pressed = pointer_event.pressed;
        touch->remote_point = pointer_event.point;
        touch->remote_active = true;
    }
    const bool pressed = touch->pressed;
    const lv_point_t point = touch->point;
    const bool remote_pressed = touch->remote_pressed;
    const lv_point_t remote_point = touch->remote_point;
    const bool remote_active = touch->remote_active;
    const bool remote_enabled = touch->remote_control_enabled;
    touch->remote_text_target_available = remote_enabled && focused_textarea;
    portEXIT_CRITICAL(&touch->lock);

    if (touch->remote_indicator != nullptr) {
        if (remote_active) lv_obj_clear_flag(touch->remote_indicator, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(touch->remote_indicator, LV_OBJ_FLAG_HIDDEN);
    }
    touch->pointer_state.Read(pressed, point, remote_pressed, remote_point, *data);
    if (has_pointer_event && pointer_event.reply) {
        pointer_event.reply(remote_enabled,
                            remote_enabled ? nullptr : "control_disabled_or_invalid");
    }
    {
        std::lock_guard<std::mutex> lock(g_remote_action_mutex);
        // 远程 pointer 事件是一个有序的时序流。不要在同一轮 LVGL 输入
        // 读取中通过 continue_reading 把 down/move/up 全部消费掉，否则
        // tileview 等手势控件只能看到最终坐标，拖拽会退化成一次点击。
        data->continue_reading = false;
    }
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
            std::lock_guard<std::mutex> queue_lock(g_remote_action_mutex);
            g_remote_pointer_events.clear();
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
        .buffer_size = static_cast<uint32_t>(lcd_cfg->lcd_width) * 40,
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
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == nullptr) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return;
    }

    lv_indev_t* touch_indev = InitTouchInput(disp);
    ui.SetPrimaryInput(touch_indev);
    ui.SetInputResetCallback(ResetTouchInputBridge, &g_touch_input);
    ui.SetPhysicalInputCallback([](void* data) {
        auto* touch = static_cast<TouchInputBridge*>(data);
        portENTER_CRITICAL(&touch->lock);
        const bool physical = !touch->remote_control_enabled && !touch->remote_active;
        portEXIT_CRITICAL(&touch->lock);
        return physical;
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
                    if (voice_assistant_service.GetState().phase ==
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
    serial_provisioning_service.SetAppLaunchCallback([](const std::string& requested_id) {
        const auto* descriptor = system.registry().ResolveAlias(requested_id);
        if (descriptor == nullptr) {
            return false;
        }
        auto* request = new SerialLaunchRequest{&system.navigation(), descriptor->id};
        if (!lvgl_port_lock(1000)) {
            delete request;
            return false;
        }
        const lv_result_t result = lv_async_call(LaunchAppFromSerial, request);
        lvgl_port_unlock();
        if (result != LV_RESULT_OK) {
            delete request;
            return false;
        }
        return true;
    });
    if (!system.Start()) {
        ESP_LOGE(TAG, "PhoneSystem start failed");
        boot_animation.Stop();
        return;
    }

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
    while (true) {
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
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
