#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/device_cloud_config.h"
#include "phone_os/device_pairing_policy.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/time_service.h"
#include "phone_os/web_file_system_service.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "rodakos_adapters/backlight_adapter.h"
#include "settings.h"

#include <esp_app_desc.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <memory>
#include <new>
#include <string>

// Settings home: grouped list on a 320x200 body. The first screen shows the 网络 and 显示 groups
// exactly (4 + 20 + 76 + 4 + 20 + 76 = 200 px); the rest scrolls.
namespace {

using namespace rodakos_settings;

constexpr lv_coord_t kGridX = 10;
constexpr lv_coord_t kGridWidth = 300;
constexpr lv_coord_t kRowHeight = 38;
constexpr lv_coord_t kGroupLabelHeight = 20;
constexpr lv_coord_t kGroupGap = 4;
constexpr lv_coord_t kValueWidth = 128;

lv_obj_t* CreateGroup(lv_obj_t* parent, const char* label, lv_coord_t& y, int rows) {
    if (label != nullptr) {
        auto* title = CreateSettingLabel(parent, label, true);
        lv_obj_set_style_text_font(title, &phone_font_12, 0);
        lv_obj_set_pos(title, kGridX + 4, y + 3);
        y += kGroupLabelHeight;
    }
    auto* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, kGridWidth, kRowHeight * rows);
    lv_obj_set_pos(list, kGridX, y);
    lv_obj_set_style_bg_color(list, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_clip_corner(list, true, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    y += kRowHeight * rows + kGroupGap;
    return list;
}

// One 38 px row. Returns the row; `value_out` receives the right-aligned status label.
lv_obj_t* CreateRow(lv_obj_t* list, int index, const char* icon, const char* title,
                    bool chevron, lv_obj_t** value_out) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, kGridWidth, kRowHeight);
    lv_obj_set_pos(row, 0, kRowHeight * index);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    if (index > 0) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, rodakos_theme_bg_tertiary(), 0);
    }

    auto* icon_label = lv_label_create(row);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_color(icon_label, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(icon_label, PhoneIconFont(), 0);
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 0, 0);

    if (title != nullptr) {
        auto* title_label = CreateSettingLabel(row, title, false);
        lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 28, 0);
    }

    lv_coord_t right = 0;
    if (chevron) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        auto* arrow = lv_label_create(row);
        lv_label_set_text(arrow, ">");
        lv_obj_set_style_text_color(arrow, rodakos_theme_text_tertiary(), 0);
        lv_obj_set_style_text_font(arrow, &phone_font_14, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
        right = -16;
    }
    if (value_out != nullptr) {
        auto* value = CreateSettingLabel(row, "", true);
        lv_obj_set_width(value, kValueWidth);
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(value, &phone_font_12, 0);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, right, 0);
        *value_out = value;
    }
    return row;
}

// Settings is destroyed while 主题 or 关于本机 is open; remember where the list was so that
// coming back lands on the same row instead of the top.
lv_coord_t g_restore_scroll_y = -1;

void RememberScroll(lv_obj_t* body) {
    g_restore_scroll_y = body != nullptr ? lv_obj_get_scroll_y(body) : -1;
}

void OpenSystemInfo(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().LaunchFrom("system", "settings");
}

std::string TimeZoneLabel(const std::string& tz) {
    size_t count = 0;
    const RodakTimeZone* zones = TimeServiceTimeZones(&count);
    for (size_t index = 0; index < count; ++index) {
        if (tz == zones[index].tz) return zones[index].label;
    }
    return tz;
}

// Status for the 连接服务 row. Load() reads several NVS keys, so it never runs on the LVGL
// thread: a short worker reads it and posts the result back with lv_async_call.
struct CloudSummaryPayload {
    std::shared_ptr<SettingsCloudRefreshGuard> guard;
    rodakos::DeviceCloudConfigService* service = nullptr;
    SettingsCloudSummary summary = SettingsCloudSummary::kUnknown;
};

void CloudSummaryDelivered(void* user_data) {
    auto* payload = static_cast<CloudSummaryPayload*>(user_data);
    SettingsApp* app = payload->guard ? payload->guard->app.load() : nullptr;
    if (app != nullptr) app->OnCloudSummaryReady(payload->summary);
    delete payload;
}

void CloudSummaryTask(void* arg) {
    auto* payload = static_cast<CloudSummaryPayload*>(arg);
    auto config = std::unique_ptr<rodakos::DeviceCloudConfig>(new (std::nothrow) rodakos::DeviceCloudConfig);
    if (config != nullptr && payload->service != nullptr) {
        payload->service->Load(*config);
        const bool bound = config->aiot_registered && config->aiot_activated && !config->unbind_pending;
        const auto diagnostic = payload->service->diagnostic();
        const bool pairing = config->has_pairing_request &&
            rodakos::ClassifyDevicePairingStatus(config->pairing_status) ==
                rodakos::DevicePairingStatus::kPending;
        if (config->server_trust_error || config->unbind_pending) {
            payload->summary = SettingsCloudSummary::kAttention;
        } else if (bound) {
            payload->summary = diagnostic == rodakos::CloudDiagnosticCode::kReady ||
                                       diagnostic == rodakos::CloudDiagnosticCode::kRefreshing
                                   ? SettingsCloudSummary::kBound
                                   : SettingsCloudSummary::kAttention;
        } else {
            payload->summary = pairing ? SettingsCloudSummary::kPairing : SettingsCloudSummary::kUnbound;
        }
    }
    bool queued = false;
    if (lvgl_port_lock(1000)) {
        queued = lv_async_call(CloudSummaryDelivered, payload) == LV_RESULT_OK;
        lvgl_port_unlock();
    }
    if (!queued) delete payload;
    vTaskDelete(nullptr);
}

}  // namespace

using namespace rodakos_settings;

void SettingsApp::CreateMainPage() {
    Settings display_settings(kDisplayNamespace, false);
    const int brightness = display_settings.GetInt(kBrightnessKey, 75);

    lv_obj_add_flag(main_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(main_body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(main_body_, LV_SCROLLBAR_MODE_AUTO);
    // Content ends exactly at the grid; no extra space below the last group.
    lv_obj_set_style_pad_bottom(main_body_, 6, 0);

    lv_coord_t y = kGroupGap;

    // ===== 网络 =====
    auto* network = CreateGroup(main_body_, "网络", y, 2);
    auto* wifi_row = CreateRow(network, 0, ICON_WIFI, "Wi-Fi", true, &home_wifi_value_);
    lv_obj_add_event_cb(wifi_row, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowPage(SettingsPage::kWiFiList);
    }, LV_EVENT_CLICKED, this);
    auto* cloud_row = CreateRow(network, 1, ICON_LINK, "连接服务", true, &home_cloud_value_);
    lv_obj_add_event_cb(cloud_row, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowPage(SettingsPage::kDeviceCloud);
    }, LV_EVENT_CLICKED, this);

    // ===== 显示 =====
    auto* display = CreateGroup(main_body_, "显示", y, 2);
    auto* brightness_row = CreateRow(display, 0, ICON_BRIGHTNESS, nullptr, false, nullptr);
    brightness_label_ = CreateSettingLabel(brightness_row, "", true);
    lv_obj_set_width(brightness_label_, 40);
    lv_obj_set_style_text_align(brightness_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(brightness_label_, &phone_font_12, 0);
    lv_obj_align(brightness_label_, LV_ALIGN_RIGHT_MID, 0, 0);
    UpdateBrightnessLabel(brightness_label_, brightness);

    brightness_slider_ = lv_slider_create(brightness_row);
    // The row spans 276 px of content: icon (28) + slider + gap (10) + value (40).
    lv_obj_set_size(brightness_slider_, 192, 6);
    lv_obj_align(brightness_slider_, LV_ALIGN_LEFT_MID, 32, 0);
    // A taller hit area than the 6 px bar, so a horizontal drag wins over list scrolling.
    lv_obj_set_ext_click_area(brightness_slider_, 14);
    lv_obj_add_flag(brightness_slider_, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(brightness_slider_, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_slider_set_range(brightness_slider_, 5, 100);
    lv_slider_set_value(brightness_slider_, brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_primary(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(brightness_slider_, 5, LV_PART_KNOB);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_bg_tertiary(), LV_PART_MAIN);

    lv_obj_add_event_cb(brightness_slider_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const int value = lv_slider_get_value(slider);
        UpdateBrightnessLabel(self->brightness_label_, value);
        if (auto* backlight = self->context_->services().backlight(); backlight != nullptr) {
            backlight->SetBrightness(static_cast<uint8_t>(value), false);
        }
    }, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(brightness_slider_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const int value = lv_slider_get_value(slider);
        Settings settings(kDisplayNamespace, true);
        settings.SetInt(kBrightnessKey, value);
        if (auto* backlight = self->context_->services().backlight(); backlight != nullptr) {
            backlight->SetBrightness(static_cast<uint8_t>(value), true);
        }
    }, LV_EVENT_RELEASED, this);

    auto* theme_row = CreateRow(display, 1, ICON_PALETTE, "主题", true, nullptr);
    lv_obj_add_event_cb(theme_row, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        RememberScroll(self->main_body_);
        lv_async_call(DeferOpenThemes, self->context_);
    }, LV_EVENT_CLICKED, this);

    // ===== 设备 =====
    auto* device = CreateGroup(main_body_, "设备", y, 3);
    auto* datetime_row = CreateRow(device, 0, ICON_CLOCK, "日期与时间", true, &home_time_zone_value_);
    lv_obj_add_event_cb(datetime_row, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowPage(SettingsPage::kDateTime);
    }, LV_EVENT_CLICKED, this);
    auto* buttons_row = CreateRow(device, 1, ICON_KEY, "按键", true, nullptr);
    lv_obj_add_event_cb(buttons_row, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowPage(SettingsPage::kButtons);
    }, LV_EVENT_CLICKED, this);
    auto* shell_row = CreateRow(device, 2, ICON_LOCK, "锁屏与控制中心", true, nullptr);
    lv_obj_add_event_cb(shell_row, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowPage(SettingsPage::kSystemShell);
    }, LV_EVENT_CLICKED, this);

    // ===== 文件传输 =====
    auto* files = CreateGroup(main_body_, "文件传输", y, 2);
    auto* web_row = CreateRow(files, 0, ICON_FOLDER, "网页传文件", true, &home_web_files_value_);
    lv_obj_add_event_cb(web_row, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowPage(SettingsPage::kWebFiles);
    }, LV_EVENT_CLICKED, this);
    lv_obj_t* usb_value = nullptr;
    auto* usb_row = CreateRow(files, 1, ICON_STORAGE, "USB 磁盘模式", true, &usb_value);
    lv_label_set_text(usb_value, "需重启");
    lv_obj_set_style_text_color(usb_value, rodakos_theme_warning(), 0);
    lv_obj_add_event_cb(usb_row, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowUsbDiskDialog();
    }, LV_EVENT_CLICKED, this);

    // ===== 关于本机 =====
    y += kGroupGap;
    auto* about = CreateGroup(main_body_, nullptr, y, 1);
    lv_obj_t* version_value = nullptr;
    auto* about_row = CreateRow(about, 0, ICON_INFO, "关于本机", true, &version_value);
    const esp_app_desc_t* app_desc = esp_app_get_description();
    lv_label_set_text(version_value, app_desc != nullptr ? app_desc->version : "");
    lv_obj_add_event_cb(about_row, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        RememberScroll(self->main_body_);
        lv_async_call(OpenSystemInfo, self->context_);
    }, LV_EVENT_CLICKED, this);

    if (g_restore_scroll_y > 0) {
        lv_obj_update_layout(main_body_);
        lv_obj_scroll_to_y(main_body_, g_restore_scroll_y, LV_ANIM_OFF);
    }
    g_restore_scroll_y = -1;

    UpdateMainPageStatus();
}

void SettingsApp::UpdateMainPageStatus() {
    if (main_body_ == nullptr || context_ == nullptr) return;

    if (home_wifi_value_ != nullptr) {
        auto* wifi = context_->services().wifi();
        const bool connected = wifi != nullptr && wifi->GetStatus() == WiFiStatus::kConnected;
        const std::string ssid = connected ? wifi->GetConnectedSSID() : std::string();
        lv_label_set_text(home_wifi_value_, connected ? ssid.c_str() : "未连接");
        lv_obj_set_style_text_color(home_wifi_value_,
                                    connected ? rodakos_theme_text_secondary() : rodakos_theme_warning(), 0);
    }
    if (home_web_files_value_ != nullptr) {
        auto* web_files = context_->services().web_files();
        lv_label_set_text(home_web_files_value_,
                          web_files != nullptr && web_files->IsRunning() ? "运行中" : "已关闭");
    }
    if (home_time_zone_value_ != nullptr) {
        lv_label_set_text(home_time_zone_value_, home_time_zone_label_.c_str());
    }
    OnCloudSummaryReady(home_cloud_summary_);

    // Refresh the NVS-backed values off the LVGL thread; results arrive via lv_async_call.
    auto* device_cloud = context_->services().device_cloud();
    if (device_cloud != nullptr && cloud_refresh_guard_ && !home_cloud_summary_pending_) {
        auto* payload = new (std::nothrow) CloudSummaryPayload;
        if (payload != nullptr) {
            payload->guard = cloud_refresh_guard_;
            payload->service = device_cloud;
            home_cloud_summary_pending_ = true;
            if (xTaskCreate(CloudSummaryTask, "settings_sum", 4096, payload, 2, nullptr) != pdPASS) {
                home_cloud_summary_pending_ = false;
                delete payload;
            }
        }
    }
    if (home_time_zone_label_.empty()) {
        // One NVS read per Settings session; the Date & Time page refreshes the cache on save.
        home_time_zone_label_ = TimeZoneLabel(TimeServiceLoadTimeZone());
        if (home_time_zone_value_ != nullptr) {
            lv_label_set_text(home_time_zone_value_, home_time_zone_label_.c_str());
        }
    }
}

void SettingsApp::OnCloudSummaryReady(SettingsCloudSummary summary) {
    home_cloud_summary_pending_ = false;
    home_cloud_summary_ = summary;
    if (home_cloud_value_ == nullptr) return;
    const char* text = "";
    lv_color_t color = rodakos_theme_text_secondary();
    switch (summary) {
        case SettingsCloudSummary::kUnknown: text = ""; break;
        case SettingsCloudSummary::kUnbound: text = "未连接"; break;
        case SettingsCloudSummary::kPairing: text = "配对中"; color = rodakos_theme_warning(); break;
        case SettingsCloudSummary::kBound: text = "已连接"; color = rodakos_theme_success(); break;
        case SettingsCloudSummary::kAttention: text = "需要处理"; color = rodakos_theme_error(); break;
    }
    lv_label_set_text(home_cloud_value_, text);
    lv_obj_set_style_text_color(home_cloud_value_, color, 0);
}
