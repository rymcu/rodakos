#include "apps/settings/settings_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_services.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/time_service.h"
#include "phone_os/web_file_system_service.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/backlight_adapter.h"
#include "settings.h"
#include "usb_msc_mode.h"

#include <esp_lvgl_port.h>
#include <esp_system.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>

namespace {
constexpr const char* TAG = "SettingsApp";
constexpr const char* kDisplayNamespace = "display";
constexpr const char* kBrightnessKey = "brightness";
constexpr const char* kThemeKey = "theme";
constexpr const char* kLanguageKey = "language";
constexpr uint32_t kTimeSyncTimeoutPolls = 20;

struct CloudRefreshPayload {
    std::shared_ptr<SettingsCloudRefreshGuard> guard;
    rodakos::DeviceCloudConfigService* service = nullptr;
    uint32_t generation = 0;
    bool ok = false;
    rodakos::DeviceCloudConfig config;
    std::string error;
};

struct ThemeOption {
    const char* id;
    const char* label;
    const char* button_label;
    rodakos_theme_preset_t preset;
    bool phone_ui_light;
    uint32_t swatch;
    uint32_t label_color;
};

constexpr std::array<ThemeOption, 4> kThemeOptions = {{
    {"light", "Light", "L", RODAKOS_THEME_LIGHT, true, 0xF7F7F7, 0x111111},
    {"dark", "Dark", "D", RODAKOS_THEME_DARK, false, 0x111111, 0xFFFFFF},
    {"blue", "Blue", "B", RODAKOS_THEME_BLUE, false, 0x1976D2, 0xFFFFFF},
    {"green", "Green", "G", RODAKOS_THEME_GREEN, false, 0x388E3C, 0xFFFFFF},
}};

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        lv_indev_reset(nullptr, nullptr);
        context->navigation().ReturnHome();
    }
}

void DeferReloadSettings(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        lv_indev_reset(nullptr, nullptr);
        context->navigation().Launch("settings");
    }
}

void RestartTimerCallback(lv_timer_t* timer) {
    lv_timer_delete(timer);
    esp_restart();
}

void CloudRefreshCompleteCallback(void* user_data) {
    auto* payload = static_cast<CloudRefreshPayload*>(user_data);
    if (payload == nullptr) {
        return;
    }
    auto guard = payload->guard;
    SettingsApp* app = guard ? guard->app.load() : nullptr;
    if (app != nullptr) {
        app->OnDeviceCloudRefreshComplete(payload->ok,
                                          payload->config,
                                          payload->error,
                                          payload->generation);
    }
    delete payload;
}

void CloudRefreshTask(void* arg) {
    auto* payload = static_cast<CloudRefreshPayload*>(arg);
    if (payload != nullptr && payload->service != nullptr) {
        payload->ok = payload->service->Refresh(payload->config);
        if (!payload->ok) {
            payload->error = payload->service->last_error();
        }
        bool queued = false;
        if (lvgl_port_lock(1000)) {
            queued = lv_async_call(CloudRefreshCompleteCallback, payload) == LV_RESULT_OK;
            lvgl_port_unlock();
        }
        if (!queued) {
            auto guard = payload->guard;
            if (guard && payload->generation == guard->refresh_generation.load()) {
                guard->refresh_in_progress.store(false);
            }
            delete payload;
        }
    } else {
        delete payload;
    }
    vTaskDelete(nullptr);
}

std::string TrimServerName(const char* text) {
    if (text == nullptr) {
        return "";
    }
    std::string value(text);
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
    if (value.size() > 63) {
        value.resize(63);
    }
    return value;
}

std::string TrimCloudUrl(const char* text) {
    if (text == nullptr) {
        return "";
    }
    std::string value(text);
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
    if (value.size() > 191) {
        value.resize(191);
    }
    return value;
}

void UpdateBrightnessLabel(lv_obj_t* label, int value) {
    if (label != nullptr) {
        lv_label_set_text_fmt(label, "%d%%", value);
    }
}

int ThemeIndexFromId(const std::string& theme) {
    for (size_t i = 0; i < kThemeOptions.size(); ++i) {
        if (theme == kThemeOptions[i].id) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void ApplyThemeToRuntime(PhoneUi* ui, const ThemeOption& option) {
    rodakos_theme_init_from_name(option.id);
    if (ui != nullptr) {
        ui->SetThemeName(option.phone_ui_light ? "light" : "dark");
    }
}

// 创建设置项卡片
lv_obj_t* CreateSettingCard(lv_obj_t* parent, lv_coord_t y_offset, lv_coord_t height = 50) {
    auto* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, height);
    lv_obj_set_pos(card, (320 - 300) / 2, y_offset);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

// 创建设置项标题
lv_obj_t* CreateSettingLabel(lv_obj_t* parent, const char* text, bool secondary = false) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label,
        secondary ? rodakos_theme_text_secondary() : rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    return label;
}

lv_obj_t* CreateSettingIcon(lv_obj_t* parent, const char* icon) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_color(label, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(label, PhoneIconFont(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return label;
}

// 创建网络列表项
lv_obj_t* CreateNetworkItem(lv_obj_t* parent, const WiFiScanResult& ap, size_t index,
                             bool is_connected, bool is_saved) {
    auto* item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, 290, 56);
    lv_obj_set_pos(item, 5, index * 60);
    lv_obj_set_style_bg_color(item, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(item, 8, 0);
    lv_obj_set_style_pad_all(item, 12, 0);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);

    // 已连接网络使用高亮边框
    if (is_connected) {
        lv_obj_set_style_border_width(item, 2, 0);
        lv_obj_set_style_border_color(item, rodakos_theme_primary(), 0);
    }

    // 悬停效果
    lv_obj_set_style_bg_color(item, rodakos_theme_primary(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_20, LV_STATE_PRESSED);

    // SSID 标签
    auto* ssid_label = lv_label_create(item);
    char ssid_text[64];
    if (is_connected) {
        snprintf(ssid_text, sizeof(ssid_text), "%s [Connected]", ap.ssid.c_str());
    } else if (is_saved) {
        snprintf(ssid_text, sizeof(ssid_text), "%s [Saved]", ap.ssid.c_str());
    } else {
        snprintf(ssid_text, sizeof(ssid_text), "%s", ap.ssid.c_str());
    }
    lv_label_set_text(ssid_label, ssid_text);
    lv_obj_set_width(ssid_label, 200);
    lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(ssid_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(ssid_label, &phone_font_14, 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // 信号强度图标（右上角，使用 Font Awesome 信号图标）
    const char* signal_icon;
    lv_color_t signal_color;
    if (ap.rssi >= -50) {
        signal_icon = FONT_AWESOME_SIGNAL_STRONG;  // 强信号
        signal_color = rodakos_theme_success();
    } else if (ap.rssi >= -70) {
        signal_icon = FONT_AWESOME_SIGNAL_GOOD;     // 中等信号
        signal_color = rodakos_theme_primary();
    } else if (ap.rssi >= -80) {
        signal_icon = FONT_AWESOME_SIGNAL_FAIR;     // 弱信号
        signal_color = rodakos_theme_warning();
    } else {
        signal_icon = FONT_AWESOME_SIGNAL_WEAK;     // 很弱
        signal_color = rodakos_theme_error();
    }

    auto* signal_label = lv_label_create(item);
    lv_label_set_text(signal_label, signal_icon);
    lv_obj_set_style_text_color(signal_label, signal_color, 0);
    lv_obj_set_style_text_font(signal_label, PhoneIconFont(), 0);
    lv_obj_align(signal_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    // 底部信息：加密状态 + 信号强度数值
    char info_text[48];
    if (ap.is_secured) {
        snprintf(info_text, sizeof(info_text), FONT_AWESOME_LOCK " Secured • %d dBm", ap.rssi);  // 加密图标
    } else {
        snprintf(info_text, sizeof(info_text), "Open • %d dBm", ap.rssi);
    }

    auto* info_label = lv_label_create(item);
    lv_label_set_text(info_label, info_text);
    lv_obj_set_style_text_color(info_label, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(info_label, &phone_font_12, 0);
    lv_obj_align(info_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    return item;
}

}  // namespace

SettingsApp::~SettingsApp() {
    OnDestroy();
}

bool SettingsApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    cloud_refresh_guard_ = std::make_shared<SettingsCloudRefreshGuard>();
    cloud_refresh_guard_->app.store(this);
    cloud_refresh_guard_->refresh_in_progress.store(false);
    cloud_refresh_guard_->refresh_generation.store(0);

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    // 从设置中加载并应用主题
    Settings display_settings(kDisplayNamespace, false);
    const std::string theme = display_settings.GetString(kThemeKey, "dark");

    rodakos_theme_init_from_name(theme.c_str());

    // 创建根容器
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Settings", [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->NavigateBack();
    }, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this, &header_title_label_);

    main_body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(main_body_);
    lv_obj_set_size(main_body_, LV_PCT(100), 200);
    lv_obj_set_pos(main_body_, 0, kRodakosAppHeaderHeight);
    lv_obj_set_style_bg_opa(main_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(main_body_, 0, 0);

    // 创建主设置页面
    CreateMainPage();

    ESP_LOGI(TAG, "Settings app created");
    return true;
}

void SettingsApp::OnDestroy() {
    if (cloud_refresh_guard_) {
        cloud_refresh_guard_->refresh_generation.fetch_add(1);
        cloud_refresh_guard_->refresh_in_progress.store(false);
        cloud_refresh_guard_->app.store(nullptr);
    }
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            CloseUsbDiskDialog();
            if (usb_disk_restart_timer_ != nullptr) {
                lv_timer_delete(usb_disk_restart_timer_);
                usb_disk_restart_timer_ = nullptr;
            }
            if (usb_disk_hint_page_ != nullptr && lv_obj_is_valid(usb_disk_hint_page_)) {
                lv_obj_delete(usb_disk_hint_page_);
            }
            usb_disk_hint_page_ = nullptr;
            CloseNtpServerDialog();
            CloseCloudProvisioningUrlDialog();
            if (time_sync_timer_ != nullptr) {
                lv_timer_delete(time_sync_timer_);
                time_sync_timer_ = nullptr;
            }
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }
    root_ = nullptr;
    main_body_ = nullptr;
    wifi_body_ = nullptr;
    wifi_detail_body_ = nullptr;
    datetime_body_ = nullptr;
    device_cloud_body_ = nullptr;
    web_upload_body_ = nullptr;
    brightness_label_ = nullptr;
    brightness_slider_ = nullptr;
    std::fill(std::begin(theme_buttons_), std::end(theme_buttons_), nullptr);
    language_switch_ = nullptr;
    usb_disk_dialog_ = nullptr;
    usb_disk_hint_page_ = nullptr;
    usb_disk_restart_timer_ = nullptr;
    header_title_label_ = nullptr;
    timezone_dropdown_ = nullptr;
    ntp_dropdown_ = nullptr;
    ntp_dialog_ = nullptr;
    ntp_textarea_ = nullptr;
    time_sync_status_label_ = nullptr;
    time_sync_timer_ = nullptr;
    time_sync_in_progress_ = false;
    time_sync_poll_count_ = 0;
    cloud_status_label_ = nullptr;
    cloud_url_label_ = nullptr;
    cloud_client_id_label_ = nullptr;
    cloud_websocket_label_ = nullptr;
    cloud_activation_label_ = nullptr;
    cloud_url_dialog_ = nullptr;
    cloud_url_textarea_ = nullptr;
    web_upload_status_label_ = nullptr;
    web_upload_url_label_ = nullptr;
    web_upload_last_label_ = nullptr;
    web_upload_start_btn_ = nullptr;
    web_upload_stop_btn_ = nullptr;
    wifi_status_label_ = nullptr;
    wifi_list_container_ = nullptr;
    detail_ssid_label_ = nullptr;
    detail_status_label_ = nullptr;
    detail_ip_label_ = nullptr;
    detail_gateway_label_ = nullptr;
    detail_netmask_label_ = nullptr;
    detail_rssi_label_ = nullptr;
    context_ = nullptr;
    ui_ = nullptr;
}

void SettingsApp::ShowPage(SettingsPage page) {
    if (page == current_page_) {
        return;
    }

    current_page_ = page;

    // 隐藏所有页面
    if (main_body_ != nullptr) {
        lv_obj_add_flag(main_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_body_ != nullptr) {
        lv_obj_add_flag(wifi_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_detail_body_ != nullptr) {
        lv_obj_add_flag(wifi_detail_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (datetime_body_ != nullptr) {
        lv_obj_add_flag(datetime_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (device_cloud_body_ != nullptr) {
        lv_obj_add_flag(device_cloud_body_, LV_OBJ_FLAG_HIDDEN);
    }
    if (web_upload_body_ != nullptr) {
        lv_obj_add_flag(web_upload_body_, LV_OBJ_FLAG_HIDDEN);
    }

    // 显示目标页面
    switch (page) {
        case SettingsPage::kMain:
            if (main_body_ != nullptr) {
                lv_obj_clear_flag(main_body_, LV_OBJ_FLAG_HIDDEN);
            }
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "Settings");
            }
            break;

        case SettingsPage::kWiFiList:
            if (wifi_body_ == nullptr) {
                CreateWiFiListPage();
            }
            lv_obj_clear_flag(wifi_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "WiFi");
            }
            StartWiFiScan();  // 自动扫描
            break;

        case SettingsPage::kWiFiDetail:
            if (wifi_detail_body_ == nullptr) {
                CreateWiFiDetailPage();
            }
            lv_obj_clear_flag(wifi_detail_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "WiFi Details");
            }
            UpdateWiFiDetailPage();  // 刷新数据
            break;

        case SettingsPage::kDateTime:
            if (datetime_body_ == nullptr) {
                CreateDateTimePage();
            }
            lv_obj_clear_flag(datetime_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "Date & Time");
            }
            break;

        case SettingsPage::kDeviceCloud:
            if (device_cloud_body_ == nullptr) {
                CreateDeviceCloudPage();
            }
            lv_obj_clear_flag(device_cloud_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "Device Services");
            }
            UpdateDeviceCloudPage();
            break;

        case SettingsPage::kWebFiles:
            if (web_upload_body_ == nullptr) {
                CreateWebFilesPage();
            }
            lv_obj_clear_flag(web_upload_body_, LV_OBJ_FLAG_HIDDEN);
            if (header_title_label_ != nullptr) {
                lv_label_set_text(header_title_label_, "Web Files");
            }
            UpdateWebFilesPage();
            break;
    }
}

void SettingsApp::CreateMainPage() {
    Settings display_settings(kDisplayNamespace, false);
    const int brightness = display_settings.GetInt(kBrightnessKey, 75);
    const std::string theme = display_settings.GetString(kThemeKey, "dark");
    const std::string language = display_settings.GetString(kLanguageKey, "en");
    const int selected_theme = ThemeIndexFromId(theme);

    lv_obj_add_flag(main_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(main_body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(main_body_, LV_SCROLLBAR_MODE_AUTO);

    // ===== 亮度设置卡片 =====
    auto* brightness_card = lv_obj_create(main_body_);
    lv_obj_remove_style_all(brightness_card);
    lv_obj_set_size(brightness_card, 300, 68);
    lv_obj_align(brightness_card, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(brightness_card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(brightness_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(brightness_card, 8, 0);
    lv_obj_set_style_pad_all(brightness_card, 12, 0);
    lv_obj_clear_flag(brightness_card, LV_OBJ_FLAG_SCROLLABLE);

    CreateSettingIcon(brightness_card, FONT_AWESOME_BRIGHTNESS);

    auto* brightness_title = CreateSettingLabel(brightness_card, "Brightness");
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 28, 0);

    brightness_label_ = CreateSettingLabel(brightness_card, "", true);
    lv_obj_align(brightness_label_, LV_ALIGN_TOP_RIGHT, 0, 0);
    UpdateBrightnessLabel(brightness_label_, brightness);

    brightness_slider_ = lv_slider_create(brightness_card);
    lv_obj_set_size(brightness_slider_, 242, 8);
    lv_obj_align(brightness_slider_, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
    lv_slider_set_range(brightness_slider_, 5, 100);
    lv_slider_set_value(brightness_slider_, brightness, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_primary(), LV_PART_KNOB);
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
        self->ui_->ShowToastUnlocked("Brightness saved");
    }, LV_EVENT_RELEASED, this);

    // ===== 主题设置卡片 =====
    auto* theme_card = CreateSettingCard(main_body_, 84);
    CreateSettingIcon(theme_card, FONT_AWESOME_MOON);

    auto* theme_title = CreateSettingLabel(theme_card, "Theme");
    lv_obj_align(theme_title, LV_ALIGN_TOP_LEFT, 28, 0);

    auto* theme_row = lv_obj_create(theme_card);
    lv_obj_remove_style_all(theme_row);
    lv_obj_set_size(theme_row, 188, 30);
    lv_obj_align(theme_row, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_flex_flow(theme_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(theme_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(theme_row, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < kThemeOptions.size(); ++i) {
        auto* btn = lv_btn_create(theme_row);
        theme_buttons_[i] = btn;
        lv_obj_set_user_data(btn, const_cast<ThemeOption*>(&kThemeOptions[i]));
        lv_obj_set_size(btn, 42, 28);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kThemeOptions[i].swatch), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, i == static_cast<size_t>(selected_theme) ? 2 : 1, 0);
        lv_obj_set_style_border_color(btn,
                                      i == static_cast<size_t>(selected_theme)
                                          ? rodakos_theme_primary()
                                          : rodakos_theme_border(),
                                      0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        auto* label = lv_label_create(btn);
        lv_label_set_text(label, kThemeOptions[i].button_label);
        lv_obj_set_style_text_color(label, lv_color_hex(kThemeOptions[i].label_color), 0);
        lv_obj_set_style_text_font(label, &phone_font_12, 0);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        auto* option = static_cast<const ThemeOption*>(lv_obj_get_user_data(btn));
        if (option != nullptr) {
            Settings settings(kDisplayNamespace, true);
            settings.SetString(kThemeKey, option->id);
            ApplyThemeToRuntime(self->ui_, *option);
            self->ui_->ShowToastUnlocked("Theme changed");
            if (auto* indev = lv_indev_active(); indev != nullptr) {
                lv_indev_wait_release(indev);
            }
            ESP_LOGI(TAG, "Theme changed to %s, reloading settings", option->id);
            lv_async_call(DeferReloadSettings, self->context_);
        }
        }, LV_EVENT_CLICKED, this);
    }

    // ===== 语言设置卡片 =====
    auto* language_card = CreateSettingCard(main_body_, 142);
    CreateSettingIcon(language_card, FONT_AWESOME_GLOBE);

    auto* language_title = CreateSettingLabel(language_card, "Chinese language");
    lv_obj_align(language_title, LV_ALIGN_LEFT_MID, 28, 0);

    language_switch_ = lv_switch_create(language_card);
    lv_obj_align(language_switch_, LV_ALIGN_RIGHT_MID, 0, 0);
    if (language == "zh") {
        lv_obj_add_state(language_switch_, LV_STATE_CHECKED);
    }

    const lv_style_selector_t checked_indicator =
        static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
        static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(language_switch_, rodakos_theme_success(), checked_indicator);
    lv_obj_set_style_bg_color(language_switch_, rodakos_theme_bg_tertiary(), LV_PART_INDICATOR);

    lv_obj_add_event_cb(language_switch_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const bool chinese = lv_obj_has_state(sw, LV_STATE_CHECKED);
        Settings settings(kDisplayNamespace, true);
        settings.SetString(kLanguageKey, chinese ? "zh" : "en");
        self->ui_->ShowToastUnlocked("Language preference saved");
    }, LV_EVENT_VALUE_CHANGED, this);

    // ===== WiFi 设置入口 =====
    auto* wifi_card = CreateSettingCard(main_body_, 200);
    lv_obj_add_flag(wifi_card, LV_OBJ_FLAG_CLICKABLE);
    CreateSettingIcon(wifi_card, FONT_AWESOME_WIFI);

    auto* wifi_title = CreateSettingLabel(wifi_card, "WiFi Settings");
    lv_obj_align(wifi_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* wifi_arrow = lv_label_create(wifi_card);
    lv_label_set_text(wifi_arrow, ">");
    lv_obj_set_style_text_color(wifi_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(wifi_arrow, &phone_font_18, 0);
    lv_obj_align(wifi_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(wifi_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kWiFiList);
    }, LV_EVENT_CLICKED, this);

    // ===== 日期与时间入口 =====
    auto* datetime_card = CreateSettingCard(main_body_, 258);
    lv_obj_add_flag(datetime_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(datetime_card, FONT_AWESOME_CLOCK);

    auto* datetime_title = CreateSettingLabel(datetime_card, "Date & Time");
    lv_obj_align(datetime_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* datetime_arrow = lv_label_create(datetime_card);
    lv_label_set_text(datetime_arrow, ">");
    lv_obj_set_style_text_color(datetime_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(datetime_arrow, &phone_font_18, 0);
    lv_obj_align(datetime_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(datetime_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kDateTime);
    }, LV_EVENT_CLICKED, this);

    // ===== 设备服务入口 =====
    auto* cloud_card = CreateSettingCard(main_body_, 316);
    lv_obj_add_flag(cloud_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(cloud_card, FONT_AWESOME_CLOUD);

    auto* cloud_title = CreateSettingLabel(cloud_card, "Device Services");
    lv_obj_align(cloud_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* cloud_arrow = lv_label_create(cloud_card);
    lv_label_set_text(cloud_arrow, ">");
    lv_obj_set_style_text_color(cloud_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(cloud_arrow, &phone_font_18, 0);
    lv_obj_align(cloud_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(cloud_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kDeviceCloud);
    }, LV_EVENT_CLICKED, this);

    // ===== Web 上传入口 =====
    auto* upload_card = CreateSettingCard(main_body_, 374);
    lv_obj_add_flag(upload_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(upload_card, FONT_AWESOME_CLOUD);

    auto* upload_title = CreateSettingLabel(upload_card, "Web Files");
    lv_obj_align(upload_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* upload_arrow = lv_label_create(upload_card);
    lv_label_set_text(upload_arrow, ">");
    lv_obj_set_style_text_color(upload_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(upload_arrow, &phone_font_18, 0);
    lv_obj_align(upload_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(upload_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kWebFiles);
    }, LV_EVENT_CLICKED, this);

    // ===== USB 磁盘模式入口 =====
    auto* usb_card = CreateSettingCard(main_body_, 432);
    lv_obj_add_flag(usb_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(usb_card, FONT_AWESOME_SD_CARD);

    auto* usb_title = CreateSettingLabel(usb_card, "USB Disk Mode");
    lv_obj_align(usb_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* usb_arrow = lv_label_create(usb_card);
    lv_label_set_text(usb_arrow, ">");
    lv_obj_set_style_text_color(usb_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(usb_arrow, &phone_font_18, 0);
    lv_obj_align(usb_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(usb_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowUsbDiskDialog();
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::ShowUsbDiskDialog() {
    if (usb_disk_dialog_ != nullptr) {
        return;
    }

    usb_disk_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(usb_disk_dialog_);
    lv_obj_set_size(usb_disk_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(usb_disk_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(usb_disk_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(usb_disk_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* dialog_box = lv_obj_create(usb_disk_dialog_);
    lv_obj_remove_style_all(dialog_box);
    lv_obj_set_size(dialog_box, 286, 154);
    lv_obj_center(dialog_box);
    lv_obj_set_style_bg_color(dialog_box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog_box, 10, 0);
    lv_obj_set_style_pad_all(dialog_box, 14, 0);
    lv_obj_clear_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(dialog_box, "USB Disk Mode");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    auto* message = CreateSettingLabel(
        dialog_box,
        "Reboot and share SD card with PC.\nSafely eject before reset.",
        true);
    lv_obj_set_width(message, 250);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 36);

    auto* cancel_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(cancel_btn, 108, 34);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_set_style_shadow_width(cancel_btn, 0, 0);

    auto* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cancel_label, &phone_font_12, 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->CloseUsbDiskDialog();
    }, LV_EVENT_CLICKED, this);

    auto* enter_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(enter_btn, 108, 34);
    lv_obj_align(enter_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(enter_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(enter_btn, 6, 0);
    lv_obj_set_style_shadow_width(enter_btn, 0, 0);

    auto* enter_label = lv_label_create(enter_btn);
    lv_label_set_text(enter_label, "Enter");
    lv_obj_set_style_text_color(enter_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(enter_label, &phone_font_12, 0);
    lv_obj_center(enter_label);
    lv_obj_add_event_cb(enter_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->EnterUsbDiskMode();
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::CloseUsbDiskDialog() {
    if (usb_disk_dialog_ != nullptr && lv_obj_is_valid(usb_disk_dialog_)) {
        lv_obj_delete(usb_disk_dialog_);
    }
    usb_disk_dialog_ = nullptr;
}

void SettingsApp::EnterUsbDiskMode() {
    if (!RequestUsbMscModeOnNextBoot()) {
        ui_->ShowToastUnlocked("USB disk request failed");
        return;
    }

    CloseUsbDiskDialog();
    ShowUsbDiskEnablePage();
}

void SettingsApp::ShowUsbDiskEnablePage() {
    if (usb_disk_hint_page_ != nullptr) {
        return;
    }

    usb_disk_hint_page_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(usb_disk_hint_page_);
    lv_obj_set_size(usb_disk_hint_page_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(usb_disk_hint_page_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(usb_disk_hint_page_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(usb_disk_hint_page_, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(title, "USB Disk Mode");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_set_style_text_color(title, rodakos_theme_text_primary(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    auto* icon = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(icon, FONT_AWESOME_SD_CARD);
    lv_obj_set_style_text_font(icon, PhoneIconFontLarge(), 0);
    lv_obj_set_style_text_color(icon, rodakos_theme_primary(), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 52);

    auto* message = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(message,
                      "Enabling USB disk...\n\n"
                      "The SD card will appear on your PC.\n"
                      "Touch is disabled in this mode.\n\n"
                      "To return:\n"
                      "1. Safely eject on the PC\n"
                      "2. Press reset or power cycle");
    lv_obj_set_width(message, 286);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message, &phone_font_12, 0);
    lv_obj_set_style_text_color(message, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 92);

    auto* footer = lv_label_create(usb_disk_hint_page_);
    lv_label_set_text(footer, "Rebooting now...");
    lv_obj_set_style_text_font(footer, &phone_font_12, 0);
    lv_obj_set_style_text_color(footer, rodakos_theme_primary(), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_move_foreground(usb_disk_hint_page_);
    lv_refr_now(nullptr);

    if (usb_disk_restart_timer_ != nullptr) {
        lv_timer_delete(usb_disk_restart_timer_);
    }
    usb_disk_restart_timer_ = lv_timer_create(RestartTimerCallback, 1200, nullptr);
    lv_timer_set_repeat_count(usb_disk_restart_timer_, 1);
}

void SettingsApp::CreateWebFilesPage() {
    web_upload_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(web_upload_body_);
    lv_obj_set_size(web_upload_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(web_upload_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(web_upload_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(web_upload_body_, 0, 0);
    lv_obj_add_flag(web_upload_body_, LV_OBJ_FLAG_HIDDEN);

    auto* status_card = CreateSettingCard(web_upload_body_, 4, 72);
    lv_obj_set_style_pad_all(status_card, 10, 0);

    auto* status_icon = lv_label_create(status_card);
    lv_label_set_text(status_icon, FONT_AWESOME_CLOUD);
    lv_obj_set_style_text_color(status_icon, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(status_icon, PhoneIconFont(), 0);
    lv_obj_align(status_icon, LV_ALIGN_LEFT_MID, 0, 0);

    auto* status_title = CreateSettingLabel(status_card, "File service", true);
    lv_obj_set_style_text_font(status_title, &phone_font_12, 0);
    lv_obj_align(status_title, LV_ALIGN_TOP_LEFT, 32, 0);

    web_upload_status_label_ = CreateSettingLabel(status_card, "Stopped", false);
    lv_obj_set_width(web_upload_status_label_, 240);
    lv_label_set_long_mode(web_upload_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(web_upload_status_label_, LV_ALIGN_TOP_LEFT, 32, 22);

    web_upload_url_label_ = CreateSettingLabel(status_card, "Start to show URL", true);
    lv_obj_set_width(web_upload_url_label_, 240);
    lv_label_set_long_mode(web_upload_url_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(web_upload_url_label_, LV_ALIGN_TOP_LEFT, 32, 42);

    auto* last_card = CreateSettingCard(web_upload_body_, 84, 50);
    lv_obj_set_style_pad_all(last_card, 10, 0);

    auto* last_title = CreateSettingLabel(last_card, "Last upload", true);
    lv_obj_set_style_text_font(last_title, &phone_font_12, 0);
    lv_obj_align(last_title, LV_ALIGN_TOP_LEFT, 0, 0);

    web_upload_last_label_ = CreateSettingLabel(last_card, "None", false);
    lv_obj_set_width(web_upload_last_label_, 276);
    lv_label_set_long_mode(web_upload_last_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(web_upload_last_label_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    auto* controls = lv_obj_create(web_upload_body_);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, 300, 38);
    lv_obj_set_pos(controls, 10, 148);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    web_upload_start_btn_ = lv_btn_create(controls);
    lv_obj_set_size(web_upload_start_btn_, 142, 34);
    lv_obj_set_style_bg_color(web_upload_start_btn_, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(web_upload_start_btn_, 6, 0);
    lv_obj_set_style_shadow_width(web_upload_start_btn_, 0, 0);
    auto* start_label = lv_label_create(web_upload_start_btn_);
    lv_label_set_text(start_label, "Start");
    lv_obj_set_style_text_color(start_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(start_label, &phone_font_12, 0);
    lv_obj_center(start_label);
    lv_obj_add_event_cb(web_upload_start_btn_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->StartWebFiles();
    }, LV_EVENT_CLICKED, this);

    web_upload_stop_btn_ = lv_btn_create(controls);
    lv_obj_set_size(web_upload_stop_btn_, 142, 34);
    lv_obj_set_style_bg_color(web_upload_stop_btn_, rodakos_theme_error(), 0);
    lv_obj_set_style_radius(web_upload_stop_btn_, 6, 0);
    lv_obj_set_style_shadow_width(web_upload_stop_btn_, 0, 0);
    auto* stop_label = lv_label_create(web_upload_stop_btn_);
    lv_label_set_text(stop_label, "Stop");
    lv_obj_set_style_text_color(stop_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(stop_label, &phone_font_12, 0);
    lv_obj_center(stop_label);
    lv_obj_add_event_cb(web_upload_stop_btn_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->StopWebFiles();
    }, LV_EVENT_CLICKED, this);

    UpdateWebFilesPage();
}

void SettingsApp::UpdateWebFilesPage() {
    auto* web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
    if (web_files == nullptr || web_upload_status_label_ == nullptr) {
        return;
    }

    const auto state = web_files->GetState();
    const std::string status = state.running
        ? (state.busy ? "Uploading..." : "Running")
        : "Stopped";
    lv_label_set_text(web_upload_status_label_, status.c_str());
    lv_label_set_text(web_upload_url_label_, state.running ? state.url.c_str() : state.message.c_str());

    if (state.last_file.empty()) {
        lv_label_set_text(web_upload_last_label_, "None");
    } else {
        char text[220] = {};
        std::snprintf(text, sizeof(text), "%s (%u KB)",
                      state.last_file.c_str(),
                      static_cast<unsigned>((state.last_bytes + 1023) / 1024));
        lv_label_set_text(web_upload_last_label_, text);
    }

    if (web_upload_start_btn_ != nullptr) {
        if (state.running) {
            lv_obj_add_state(web_upload_start_btn_, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(web_upload_start_btn_, LV_STATE_DISABLED);
        }
    }
    if (web_upload_stop_btn_ != nullptr) {
        if (state.running) {
            lv_obj_clear_state(web_upload_stop_btn_, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(web_upload_stop_btn_, LV_STATE_DISABLED);
        }
    }
}

void SettingsApp::StartWebFiles() {
    auto* wifi = context_ != nullptr ? context_->services().wifi() : nullptr;
    auto* web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
    if (wifi == nullptr || web_files == nullptr) {
        ui_->ShowToastUnlocked("Web files unavailable");
        return;
    }
    if (wifi->GetStatus() != WiFiStatus::kConnected || wifi->GetIPAddress().empty()) {
        ui_->ShowToastUnlocked("Connect WiFi first");
        if (web_upload_status_label_ != nullptr) {
            lv_label_set_text(web_upload_status_label_, "WiFi not connected");
        }
        return;
    }

    if (web_files->Start(wifi->GetIPAddress())) {
        const auto state = web_files->GetState();
        ui_->ShowToastUnlocked("Web files started");
        if (web_upload_url_label_ != nullptr) {
            lv_label_set_text(web_upload_url_label_, state.url.c_str());
        }
    } else {
        const auto state = web_files->GetState();
        ui_->ShowToastUnlocked("Web files failed");
        if (web_upload_status_label_ != nullptr) {
            lv_label_set_text(web_upload_status_label_, state.message.c_str());
        }
    }
    UpdateWebFilesPage();
}

void SettingsApp::StopWebFiles() {
    auto* web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
    if (web_files == nullptr) {
        return;
    }
    web_files->Stop();
    ui_->ShowToastUnlocked("Web files stopped");
    UpdateWebFilesPage();
}

void SettingsApp::CreateDeviceCloudPage() {
    device_cloud_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(device_cloud_body_);
    lv_obj_set_size(device_cloud_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(device_cloud_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(device_cloud_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(device_cloud_body_, 0, 0);
    lv_obj_add_flag(device_cloud_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(device_cloud_body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(device_cloud_body_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(device_cloud_body_, LV_OBJ_FLAG_HIDDEN);

    auto* status_card = CreateSettingCard(device_cloud_body_, 4, 50);
    lv_obj_set_style_pad_all(status_card, 10, 0);

    auto* status_icon = lv_label_create(status_card);
    lv_label_set_text(status_icon, FONT_AWESOME_CLOUD);
    lv_obj_set_style_text_color(status_icon, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(status_icon, PhoneIconFont(), 0);
    lv_obj_align(status_icon, LV_ALIGN_LEFT_MID, 0, 0);

    auto* status_title = CreateSettingLabel(status_card, "System configuration", true);
    lv_obj_set_style_text_font(status_title, &phone_font_12, 0);
    lv_obj_align(status_title, LV_ALIGN_TOP_LEFT, 32, 0);

    cloud_status_label_ = CreateSettingLabel(status_card, "Idle", false);
    lv_obj_set_width(cloud_status_label_, 236);
    lv_label_set_long_mode(cloud_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(cloud_status_label_, LV_ALIGN_BOTTOM_LEFT, 32, 0);

    auto* url_card = CreateSettingCard(device_cloud_body_, 62, 62);
    lv_obj_set_style_pad_all(url_card, 8, 0);

    auto* url_icon = lv_label_create(url_card);
    lv_label_set_text(url_icon, FONT_AWESOME_LINK);
    lv_obj_set_style_text_color(url_icon, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(url_icon, PhoneIconFont(), 0);
    lv_obj_align(url_icon, LV_ALIGN_LEFT_MID, 0, 0);

    auto* url_title = CreateSettingLabel(url_card, "Provisioning endpoint", true);
    lv_obj_set_style_text_font(url_title, &phone_font_12, 0);
    lv_obj_set_width(url_title, 196);
    lv_label_set_long_mode(url_title, LV_LABEL_LONG_DOT);
    lv_obj_align(url_title, LV_ALIGN_TOP_LEFT, 32, 0);

    cloud_url_label_ = CreateSettingLabel(url_card, "", false);
    lv_obj_set_width(cloud_url_label_, 196);
    lv_label_set_long_mode(cloud_url_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(cloud_url_label_, LV_ALIGN_BOTTOM_LEFT, 32, 0);

    auto* edit_btn = lv_btn_create(url_card);
    lv_obj_remove_style_all(edit_btn);
    lv_obj_set_size(edit_btn, 32, 28);
    lv_obj_align(edit_btn, LV_ALIGN_RIGHT_MID, -36, 0);
    lv_obj_set_style_bg_color(edit_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(edit_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(edit_btn, 6, 0);
    lv_obj_clear_flag(edit_btn, LV_OBJ_FLAG_SCROLLABLE);
    auto* edit_icon = lv_label_create(edit_btn);
    lv_label_set_text(edit_icon, FONT_AWESOME_PEN_TO_SQUARE);
    lv_obj_set_style_text_color(edit_icon, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(edit_icon, PhoneIconFont(), 0);
    lv_obj_center(edit_icon);
    lv_obj_add_event_cb(edit_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowCloudProvisioningUrlDialog();
    }, LV_EVENT_CLICKED, this);

    auto* refresh_btn = lv_btn_create(url_card);
    lv_obj_remove_style_all(refresh_btn);
    lv_obj_set_size(refresh_btn, 32, 28);
    lv_obj_align(refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(refresh_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(refresh_btn, 6, 0);
    lv_obj_clear_flag(refresh_btn, LV_OBJ_FLAG_SCROLLABLE);
    auto* refresh_icon = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_icon, FONT_AWESOME_ARROWS_ROTATE);
    lv_obj_set_style_text_color(refresh_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(refresh_icon, PhoneIconFont(), 0);
    lv_obj_center(refresh_icon);
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->RefreshDeviceCloud();
    }, LV_EVENT_CLICKED, this);

    auto* id_card = CreateSettingCard(device_cloud_body_, 132, 40);
    lv_obj_set_style_pad_all(id_card, 8, 0);
    cloud_client_id_label_ = CreateSettingLabel(id_card, "Device ID", true);
    lv_obj_set_width(cloud_client_id_label_, 264);
    lv_label_set_long_mode(cloud_client_id_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(cloud_client_id_label_, LV_ALIGN_LEFT_MID, 0, 0);

    auto* ws_card = CreateSettingCard(device_cloud_body_, 180, 40);
    lv_obj_set_style_pad_all(ws_card, 8, 0);
    cloud_websocket_label_ = CreateSettingLabel(ws_card, "Realtime service: not configured", true);
    lv_obj_set_width(cloud_websocket_label_, 264);
    lv_label_set_long_mode(cloud_websocket_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(cloud_websocket_label_, LV_ALIGN_LEFT_MID, 0, 0);

    auto* activation_card = CreateSettingCard(device_cloud_body_, 228, 40);
    lv_obj_set_style_pad_all(activation_card, 8, 0);
    cloud_activation_label_ = CreateSettingLabel(activation_card, "Activation: not required", true);
    lv_obj_set_width(cloud_activation_label_, 264);
    lv_label_set_long_mode(cloud_activation_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(cloud_activation_label_, LV_ALIGN_LEFT_MID, 0, 0);

    UpdateDeviceCloudPage();
}

void SettingsApp::UpdateDeviceCloudPage() {
    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (device_cloud == nullptr || cloud_status_label_ == nullptr) {
        return;
    }

    rodakos::DeviceCloudConfig config;
    device_cloud->Load(config);
    lv_label_set_text(cloud_status_label_,
                      config.has_websocket_config ? "Ready" : "Refresh required");
    lv_label_set_text(cloud_url_label_, config.provisioning_url.c_str());
    const std::string client_id = "Device ID: " + device_cloud->GetClientId();
    lv_label_set_text(cloud_client_id_label_, client_id.c_str());
    lv_label_set_text_fmt(cloud_websocket_label_, "Realtime service: %s",
                          config.has_websocket_config ? "configured" : "not configured");
    if (config.has_activation_code) {
        lv_label_set_text_fmt(cloud_activation_label_, "Activation: %s",
                              config.activation_code.c_str());
    } else {
        lv_label_set_text(cloud_activation_label_, "Activation: not required");
    }
}

void SettingsApp::RefreshDeviceCloud() {
    auto* wifi = context_ != nullptr ? context_->services().wifi() : nullptr;
    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (device_cloud == nullptr) {
        ui_->ShowToastUnlocked("Device services unavailable");
        return;
    }
    if (wifi == nullptr || wifi->GetStatus() != WiFiStatus::kConnected) {
        ui_->ShowToastUnlocked("Connect WiFi first");
        if (cloud_status_label_ != nullptr) {
            lv_label_set_text(cloud_status_label_, "WiFi not connected");
        }
        return;
    }

    if (!cloud_refresh_guard_) {
        ui_->ShowToastUnlocked("Device services unavailable");
        return;
    }
    bool expected = false;
    if (!cloud_refresh_guard_->refresh_in_progress.compare_exchange_strong(expected, true)) {
        ui_->ShowToastUnlocked("Device services refreshing");
        return;
    }

    if (cloud_status_label_ != nullptr) {
        lv_label_set_text(cloud_status_label_, "Refreshing...");
    }

    auto* payload = new CloudRefreshPayload;
    payload->guard = cloud_refresh_guard_;
    payload->service = device_cloud;
    payload->generation = cloud_refresh_guard_->refresh_generation.load();

    const BaseType_t ret = xTaskCreate(
        CloudRefreshTask, "cloud_refresh", 6144, payload, 3, nullptr);
    if (ret != pdPASS) {
        cloud_refresh_guard_->refresh_in_progress.store(false);
        delete payload;
        ui_->ShowToastUnlocked("Device services failed");
        if (cloud_status_label_ != nullptr) {
            lv_label_set_text(cloud_status_label_, "Refresh task failed");
        }
    }
}

void SettingsApp::OnDeviceCloudRefreshComplete(bool ok,
                                               const rodakos::DeviceCloudConfig& config,
                                               const std::string& error,
                                               uint32_t generation) {
    if (ui_ == nullptr) {
        return;
    }
    if (!cloud_refresh_guard_) {
        return;
    }
    if (generation != cloud_refresh_guard_->refresh_generation.load()) {
        return;
    }

    cloud_refresh_guard_->refresh_in_progress.store(false);
    UpdateDeviceCloudPage();
    if (ok) {
        ui_->ShowToastUnlocked("Device services updated");
        if (cloud_status_label_ != nullptr) {
            lv_label_set_text(cloud_status_label_, "Ready");
        }
    } else {
        ui_->ShowToastUnlocked("Device services failed");
        if (cloud_status_label_ != nullptr) {
            lv_label_set_text(cloud_status_label_, error.empty() ? "Refresh failed" : error.c_str());
        }
        if (config.has_activation_code && cloud_activation_label_ != nullptr) {
            lv_label_set_text_fmt(cloud_activation_label_, "Activation: %s",
                                  config.activation_code.c_str());
        }
    }
}

void SettingsApp::ShowCloudProvisioningUrlDialog() {
    if (cloud_url_dialog_ != nullptr) {
        return;
    }

    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    rodakos::DeviceCloudConfig config;
    if (device_cloud != nullptr) {
        device_cloud->Load(config);
    }
    if (config.provisioning_url.empty()) {
        config.provisioning_url = rodakos::DeviceCloudConfigService::DefaultProvisioningUrl();
    }

    cloud_url_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(cloud_url_dialog_);
    lv_obj_set_size(cloud_url_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cloud_url_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cloud_url_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(cloud_url_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* dialog_box = lv_obj_create(cloud_url_dialog_);
    lv_obj_remove_style_all(dialog_box);
    lv_obj_set_size(dialog_box, 288, 152);
    lv_obj_align(dialog_box, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(dialog_box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog_box, 8, 0);
    lv_obj_set_style_pad_all(dialog_box, 12, 0);
    lv_obj_clear_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(dialog_box, "Provisioning endpoint");
    lv_obj_set_style_text_font(title, &phone_font_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    cloud_url_textarea_ = lv_textarea_create(dialog_box);
    lv_obj_set_size(cloud_url_textarea_, 264, 42);
    lv_obj_align(cloud_url_textarea_, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_textarea_set_one_line(cloud_url_textarea_, true);
    lv_textarea_set_max_length(cloud_url_textarea_, 191);
    lv_textarea_set_text(cloud_url_textarea_, config.provisioning_url.c_str());
    lv_textarea_set_placeholder_text(cloud_url_textarea_, rodakos::DeviceCloudConfigService::DefaultProvisioningUrl());
    lv_obj_set_style_bg_color(cloud_url_textarea_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(cloud_url_textarea_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cloud_url_textarea_, &phone_font_12, 0);
    lv_obj_set_style_border_width(cloud_url_textarea_, 0, 0);
    lv_obj_set_style_radius(cloud_url_textarea_, 6, 0);

    auto* cancel_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(cancel_btn, 126, 30);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_set_style_shadow_width(cancel_btn, 0, 0);

    auto* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cancel_label, &phone_font_12, 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->CloseCloudProvisioningUrlDialog();
    }, LV_EVENT_CLICKED, this);

    auto* save_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(save_btn, 126, 30);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(save_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(save_btn, 6, 0);
    lv_obj_set_style_shadow_width(save_btn, 0, 0);

    auto* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_set_style_text_color(save_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(save_label, &phone_font_12, 0);
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->SaveCloudProvisioningUrl(TrimCloudUrl(lv_textarea_get_text(self->cloud_url_textarea_)));
        self->CloseCloudProvisioningUrlDialog();
    }, LV_EVENT_CLICKED, this);

    soft_keyboard_.Show(cloud_url_textarea_, [this]() {
        SaveCloudProvisioningUrl(TrimCloudUrl(lv_textarea_get_text(cloud_url_textarea_)));
        CloseCloudProvisioningUrlDialogAsync();
    });
}

void SettingsApp::CloseCloudProvisioningUrlDialog() {
    soft_keyboard_.Hide();
    if (cloud_url_dialog_ != nullptr && lv_obj_is_valid(cloud_url_dialog_)) {
        lv_obj_delete(cloud_url_dialog_);
    }
    cloud_url_dialog_ = nullptr;
    cloud_url_textarea_ = nullptr;
}

void SettingsApp::CloseCloudProvisioningUrlDialogAsync() {
    lv_async_call([](void* user_data) {
        auto* self = static_cast<SettingsApp*>(user_data);
        if (self != nullptr) {
            self->CloseCloudProvisioningUrlDialog();
        }
    }, this);
}

void SettingsApp::SaveCloudProvisioningUrl(const std::string& url) {
    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (device_cloud == nullptr) {
        return;
    }
    device_cloud->SaveProvisioningUrl(url);
    ui_->ShowToastUnlocked("Provisioning endpoint saved");
    UpdateDeviceCloudPage();
}

void SettingsApp::NavigateBack() {
    if (current_page_ == SettingsPage::kWiFiDetail) {
        ShowPage(SettingsPage::kWiFiList);
        return;
    }
    if (current_page_ == SettingsPage::kWiFiList ||
        current_page_ == SettingsPage::kDateTime ||
        current_page_ == SettingsPage::kDeviceCloud ||
        current_page_ == SettingsPage::kWebFiles) {
        ShowPage(SettingsPage::kMain);
        return;
    }
    NavigateHome();
}

void SettingsApp::NavigateHome() {
    if (auto* indev = lv_indev_active(); indev != nullptr) {
        lv_indev_wait_release(indev);
    }
    ESP_LOGI(TAG, "Header home button returning home");
    lv_async_call(DeferReturnHome, context_);
}

void SettingsApp::CreateDateTimePage() {
    datetime_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(datetime_body_);
    lv_obj_set_size(datetime_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(datetime_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(datetime_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(datetime_body_, 0, 0);
    lv_obj_add_flag(datetime_body_, LV_OBJ_FLAG_HIDDEN);

    auto* tz_card = CreateSettingCard(datetime_body_, 4, 64);
    lv_obj_set_style_pad_all(tz_card, 8, 0);
    auto* tz_icon = lv_label_create(tz_card);
    lv_label_set_text(tz_icon, FONT_AWESOME_GLOBE);
    lv_obj_set_style_text_color(tz_icon, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(tz_icon, PhoneIconFont(), 0);
    lv_obj_align(tz_icon, LV_ALIGN_LEFT_MID, 0, 0);

    auto* tz_title = CreateSettingLabel(tz_card, "Time zone", true);
    lv_obj_set_style_text_font(tz_title, &phone_font_12, 0);
    lv_obj_set_width(tz_title, 236);
    lv_label_set_long_mode(tz_title, LV_LABEL_LONG_DOT);
    lv_obj_align(tz_title, LV_ALIGN_TOP_LEFT, 32, -1);

    timezone_dropdown_ = lv_dropdown_create(tz_card);
    lv_dropdown_set_options(timezone_dropdown_,
                            "Shanghai (UTC+8)\nUTC\nTokyo (UTC+9)\nLos Angeles\nNew York\nLondon\nBerlin");
    lv_dropdown_set_selected(
        timezone_dropdown_,
        static_cast<uint16_t>(TimeServiceFindTimeZoneIndex(TimeServiceLoadTimeZone())));
    lv_obj_set_size(timezone_dropdown_, 236, 28);
    lv_obj_align(timezone_dropdown_, LV_ALIGN_BOTTOM_LEFT, 32, 0);
    lv_obj_set_style_bg_color(timezone_dropdown_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(timezone_dropdown_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(timezone_dropdown_, &phone_font_12, 0);
    lv_obj_set_style_border_width(timezone_dropdown_, 0, 0);
    lv_obj_set_style_radius(timezone_dropdown_, 6, 0);
    lv_obj_add_event_cb(timezone_dropdown_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
        self->SaveTimeZone(lv_dropdown_get_selected(dropdown));
    }, LV_EVENT_VALUE_CHANGED, this);

    auto* server_card = CreateSettingCard(datetime_body_, 72, 64);
    lv_obj_set_style_pad_all(server_card, 8, 0);
    auto* server_icon = lv_label_create(server_card);
    lv_label_set_text(server_icon, FONT_AWESOME_CLOUD);
    lv_obj_set_style_text_color(server_icon, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(server_icon, PhoneIconFont(), 0);
    lv_obj_align(server_icon, LV_ALIGN_LEFT_MID, 0, 0);

    auto* server_title = CreateSettingLabel(server_card, "Time server", true);
    lv_obj_set_style_text_font(server_title, &phone_font_12, 0);
    lv_obj_set_width(server_title, 236);
    lv_label_set_long_mode(server_title, LV_LABEL_LONG_DOT);
    lv_obj_align(server_title, LV_ALIGN_TOP_LEFT, 32, -1);

    ntp_dropdown_ = lv_dropdown_create(server_card);
    lv_dropdown_set_options(ntp_dropdown_,
                            "pool.ntp.org\n0.pool.ntp.org\n1.pool.ntp.org\nntp.aliyun.com\nTencent CN\nApple\nGoogle\nCustom");
    lv_dropdown_set_selected(
        ntp_dropdown_,
        static_cast<uint16_t>(TimeServiceFindNtpServerIndex(TimeServiceLoadNtpServer())));
    lv_obj_set_size(ntp_dropdown_, 166, 28);
    lv_obj_align(ntp_dropdown_, LV_ALIGN_BOTTOM_LEFT, 32, 0);
    lv_obj_set_style_bg_color(ntp_dropdown_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(ntp_dropdown_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(ntp_dropdown_, &phone_font_12, 0);
    lv_obj_set_style_border_width(ntp_dropdown_, 0, 0);
    lv_obj_set_style_radius(ntp_dropdown_, 6, 0);
    lv_obj_add_event_cb(ntp_dropdown_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const size_t index = lv_dropdown_get_selected(dropdown);
        size_t count = 0;
        const auto* servers = TimeServiceNtpServers(&count);
        if (index >= count) {
            return;
        }
        if (servers[index].server[0] == '\0') {
            self->ShowNtpServerDialog();
            return;
        }
        self->SaveNtpServer(servers[index].server);
    }, LV_EVENT_VALUE_CHANGED, this);

    auto* edit_btn = lv_btn_create(server_card);
    lv_obj_remove_style_all(edit_btn);
    lv_obj_set_size(edit_btn, 36, 28);
    lv_obj_align(edit_btn, LV_ALIGN_BOTTOM_RIGHT, -42, 0);
    lv_obj_set_style_bg_color(edit_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(edit_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(edit_btn, 6, 0);
    lv_obj_clear_flag(edit_btn, LV_OBJ_FLAG_SCROLLABLE);
    auto* edit_icon = lv_label_create(edit_btn);
    lv_label_set_text(edit_icon, FONT_AWESOME_PEN_TO_SQUARE);
    lv_obj_set_style_text_color(edit_icon, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(edit_icon, PhoneIconFont(), 0);
    lv_obj_center(edit_icon);
    lv_obj_add_event_cb(edit_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowNtpServerDialog();
    }, LV_EVENT_CLICKED, this);

    auto* sync_btn = lv_btn_create(server_card);
    lv_obj_remove_style_all(sync_btn);
    lv_obj_set_size(sync_btn, 36, 28);
    lv_obj_align(sync_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(sync_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(sync_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sync_btn, 6, 0);
    lv_obj_clear_flag(sync_btn, LV_OBJ_FLAG_SCROLLABLE);
    auto* sync_icon = lv_label_create(sync_btn);
    lv_label_set_text(sync_icon, FONT_AWESOME_ARROWS_ROTATE);
    lv_obj_set_style_text_color(sync_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(sync_icon, PhoneIconFont(), 0);
    lv_obj_center(sync_icon);
    lv_obj_add_event_cb(sync_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->StartTimeSync();
    }, LV_EVENT_CLICKED, this);

    auto* status_card = CreateSettingCard(datetime_body_, 144, 40);
    lv_obj_set_style_pad_all(status_card, 8, 0);
    time_sync_status_label_ = CreateSettingLabel(status_card, "Sync status: idle", true);
    lv_obj_set_style_text_font(time_sync_status_label_, &phone_font_12, 0);
    lv_obj_set_width(time_sync_status_label_, 264);
    lv_label_set_long_mode(time_sync_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(time_sync_status_label_, LV_ALIGN_LEFT_MID, 0, 0);
}

void SettingsApp::CreateWiFiListPage() {
    // 创建 WiFi 页面的 body 容器
    wifi_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(wifi_body_);
    lv_obj_set_size(wifi_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(wifi_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(wifi_body_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(wifi_body_, LV_OBJ_FLAG_HIDDEN);  // 默认隐藏

    // 顶部信息区域
    auto* info_container = lv_obj_create(wifi_body_);
    lv_obj_remove_style_all(info_container);
    lv_obj_set_size(info_container, 300, 60);
    lv_obj_align(info_container, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(info_container, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(info_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(info_container, 8, 0);
    lv_obj_set_style_pad_all(info_container, 12, 0);
    lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);

    // 状态图标
    auto* status_icon = lv_label_create(info_container);
    lv_label_set_text(status_icon, FONT_AWESOME_WIFI);
    lv_obj_set_style_text_font(status_icon, PhoneIconFont(), 0);
    lv_obj_align(status_icon, LV_ALIGN_LEFT_MID, 0, 0);

    // 状态文字
    wifi_status_label_ = lv_label_create(info_container);
    lv_label_set_text(wifi_status_label_, "Tap 'Scan' to search");
    lv_obj_set_width(wifi_status_label_, 220);
    lv_label_set_long_mode(wifi_status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(wifi_status_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(wifi_status_label_, &phone_font_12, 0);
    lv_obj_align(wifi_status_label_, LV_ALIGN_LEFT_MID, 35, 0);

    // 扫描按钮（右侧）
    auto* scan_btn = lv_btn_create(info_container);
    lv_obj_set_size(scan_btn, 60, 30);
    lv_obj_align(scan_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(scan_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(scan_btn, 6, 0);

    auto* scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_set_style_text_color(scan_label, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_text_font(scan_label, &phone_font_12, 0);
    lv_obj_center(scan_label);

    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->StartWiFiScan();
    }, LV_EVENT_CLICKED, this);

    // 网络列表容器
    wifi_list_container_ = lv_obj_create(wifi_body_);
    lv_obj_remove_style_all(wifi_list_container_);
    lv_obj_set_size(wifi_list_container_, 300, 115);
    lv_obj_set_pos(wifi_list_container_, 10, 73);
    lv_obj_set_style_bg_opa(wifi_list_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(wifi_list_container_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(wifi_list_container_, LV_DIR_VER);

    // 提示文字
    auto* hint_label = lv_label_create(wifi_body_);
    lv_label_set_text(hint_label, "Tap network to connect");
    lv_obj_set_style_text_color(hint_label, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(hint_label, &phone_font_12, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -5);
}

void SettingsApp::SaveTimeZone(size_t index) {
    size_t count = 0;
    const auto* zones = TimeServiceTimeZones(&count);
    if (index >= count) {
        return;
    }

    TimeServiceSaveTimeZone(zones[index].tz);
    TimeServiceApplyTimeZone(zones[index].tz);
    ui_->ShowToastUnlocked("Time zone saved");
    ESP_LOGI(TAG, "Time zone changed: %s (%s)", zones[index].label, zones[index].tz);
}

void SettingsApp::SaveNtpServer(const std::string& server) {
    if (server.empty()) {
        ui_->ShowToastUnlocked("Server name is empty");
        return;
    }

    TimeServiceSaveNtpServer(server);
    if (ntp_dropdown_ != nullptr) {
        lv_dropdown_set_selected(
            ntp_dropdown_,
            static_cast<uint16_t>(TimeServiceFindNtpServerIndex(server)));
    }
    ui_->ShowToastUnlocked("NTP server saved");
    ESP_LOGI(TAG, "NTP server changed: %s", server.c_str());
}

void SettingsApp::StartTimeSync() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr || wifi->GetStatus() != WiFiStatus::kConnected) {
        if (time_sync_status_label_ != nullptr) {
            lv_label_set_text(time_sync_status_label_, "Sync status: WiFi not connected");
            lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_warning(), 0);
        }
        ui_->ShowToastUnlocked("Connect WiFi first");
        return;
    }

    if (time_sync_timer_ != nullptr) {
        lv_timer_delete(time_sync_timer_);
        time_sync_timer_ = nullptr;
    }

    if (!TimeServiceStartSavedSync()) {
        if (time_sync_status_label_ != nullptr) {
            lv_label_set_text(time_sync_status_label_, "Sync status: failed");
            lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_warning(), 0);
        }
        return;
    }

    time_sync_in_progress_ = true;
    time_sync_poll_count_ = 0;
    if (time_sync_status_label_ != nullptr) {
        lv_label_set_text(time_sync_status_label_, "Sync status: syncing...");
        lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_primary(), 0);
    }
    time_sync_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
        if (self != nullptr) {
            self->UpdateTimeSyncStatus();
        }
    }, 1000, this);
    ui_->ShowToastUnlocked("Time sync started");
}

void SettingsApp::UpdateTimeSyncStatus() {
    if (!time_sync_in_progress_) {
        return;
    }

    time_sync_poll_count_++;
    const TimeSyncStatus status = TimeServiceGetSyncStatus();
    if (status == TimeSyncStatus::kCompleted) {
        time_sync_in_progress_ = false;
        if (time_sync_timer_ != nullptr) {
            lv_timer_delete(time_sync_timer_);
            time_sync_timer_ = nullptr;
        }
        if (time_sync_status_label_ != nullptr) {
            lv_label_set_text(time_sync_status_label_, "Sync status: synced");
            lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_success(), 0);
        }
        ui_->ShowToastUnlocked("Time synced");
        return;
    }

    if (time_sync_poll_count_ >= kTimeSyncTimeoutPolls) {
        time_sync_in_progress_ = false;
        if (time_sync_timer_ != nullptr) {
            lv_timer_delete(time_sync_timer_);
            time_sync_timer_ = nullptr;
        }
        if (time_sync_status_label_ != nullptr) {
            lv_label_set_text(time_sync_status_label_, "Sync status: timeout");
            lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_warning(), 0);
        }
        ui_->ShowToastUnlocked("Sync timeout");
        return;
    }

    if (time_sync_status_label_ != nullptr) {
        lv_label_set_text_fmt(time_sync_status_label_, "Sync status: syncing... %lus",
                              static_cast<unsigned long>(time_sync_poll_count_));
    }
}

void SettingsApp::ShowNtpServerDialog() {
    if (ntp_dialog_ != nullptr) {
        return;
    }
    if (ntp_dropdown_ != nullptr) {
        lv_dropdown_set_selected(
            ntp_dropdown_,
            static_cast<uint16_t>(TimeServiceFindNtpServerIndex(TimeServiceLoadNtpServer())));
    }

    ntp_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(ntp_dialog_);
    lv_obj_set_size(ntp_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ntp_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ntp_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(ntp_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* dialog_box = lv_obj_create(ntp_dialog_);
    lv_obj_remove_style_all(dialog_box);
    lv_obj_set_size(dialog_box, 288, 112);
    lv_obj_align(dialog_box, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_bg_color(dialog_box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog_box, 10, 0);
    lv_obj_set_style_pad_all(dialog_box, 14, 0);
    lv_obj_clear_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(dialog_box, "NTP Server");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    ntp_textarea_ = lv_textarea_create(dialog_box);
    lv_obj_set_size(ntp_textarea_, 252, 32);
    lv_obj_align(ntp_textarea_, LV_ALIGN_TOP_MID, 0, 28);
    lv_textarea_set_one_line(ntp_textarea_, true);
    lv_textarea_set_max_length(ntp_textarea_, 63);
    lv_textarea_set_text(ntp_textarea_, TimeServiceLoadNtpServer().c_str());
    lv_textarea_set_placeholder_text(ntp_textarea_, "pool.ntp.org");
    lv_obj_set_style_bg_color(ntp_textarea_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(ntp_textarea_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_border_color(ntp_textarea_, rodakos_theme_primary(), LV_STATE_FOCUSED);

    auto* cancel_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(cancel_btn, 108, 28);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);
    lv_obj_set_style_shadow_width(cancel_btn, 0, 0);

    auto* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cancel_label, &phone_font_12, 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->CloseNtpServerDialog();
    }, LV_EVENT_CLICKED, this);

    auto* save_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(save_btn, 108, 28);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(save_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(save_btn, 6, 0);
    lv_obj_set_style_shadow_width(save_btn, 0, 0);

    auto* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_set_style_text_color(save_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(save_label, &phone_font_12, 0);
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        const std::string server = TrimServerName(lv_textarea_get_text(self->ntp_textarea_));
        self->SaveNtpServer(server);
        self->CloseNtpServerDialog();
    }, LV_EVENT_CLICKED, this);

    soft_keyboard_.Show(ntp_textarea_, [this]() {
        const std::string server = TrimServerName(lv_textarea_get_text(ntp_textarea_));
        SaveNtpServer(server);
        CloseNtpServerDialogAsync();
    });
}

void SettingsApp::CloseNtpServerDialog() {
    soft_keyboard_.Hide();
    if (ntp_dialog_ != nullptr && lv_obj_is_valid(ntp_dialog_)) {
        lv_obj_delete(ntp_dialog_);
    }
    ntp_dialog_ = nullptr;
    ntp_textarea_ = nullptr;
}

void SettingsApp::CloseNtpServerDialogAsync() {
    lv_async_call([](void* user_data) {
        auto* self = static_cast<SettingsApp*>(user_data);
        if (self != nullptr) {
            self->CloseNtpServerDialog();
        }
    }, this);
}

void SettingsApp::StartWiFiScan() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr) {
        ui_->ShowToastUnlocked("WiFi not available");
        return;
    }

    lv_label_set_text(wifi_status_label_, "Scanning...");
    lv_obj_clean(wifi_list_container_);

    wifi->StartScan([this](const std::vector<WiFiScanResult>& results) {
        wifi_scan_results_ = results;
        lv_async_call([](void* user_data) {
            auto* self = static_cast<SettingsApp*>(user_data);
            self->OnWiFiScanComplete(self->wifi_scan_results_);
        }, this);
    });
}

void SettingsApp::OnWiFiScanComplete(const std::vector<WiFiScanResult>& results) {
    if (results.empty()) {
        lv_label_set_text(wifi_status_label_, "No networks found");
        return;
    }

    char status_text[64];
    snprintf(status_text, sizeof(status_text), "Found %zu network(s)", results.size());
    lv_label_set_text(wifi_status_label_, status_text);

    // 获取保存的 SSID（用于标记已保存网络）
    std::string saved_ssid = wifi_config_.GetSavedSSID();

    // 获取当前连接状态
    auto* wifi = context_->services().wifi();
    bool is_connected = wifi && (wifi->GetStatus() == WiFiStatus::kConnected);

    lv_obj_clean(wifi_list_container_);

    for (size_t i = 0; i < results.size(); ++i) {
        // 如果已连接且是保存的网络，则标记为已连接
        bool is_current = is_connected && (results[i].ssid == saved_ssid);
        bool is_saved = (results[i].ssid == saved_ssid);

        auto* item = CreateNetworkItem(wifi_list_container_, results[i], i, is_current, is_saved);
        lv_obj_set_user_data(item, (void*)i);

        lv_obj_add_event_cb(item, [](lv_event_t* e) {
            auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
            auto* item = static_cast<lv_obj_t*>(lv_event_get_target(e));
            size_t index = reinterpret_cast<size_t>(lv_obj_get_user_data(item));

            if (index < self->wifi_scan_results_.size()) {
                const auto& ap = self->wifi_scan_results_[index];
                self->OnNetworkSelected(ap.ssid, ap.is_secured);
            }
        }, LV_EVENT_CLICKED, this);
    }

    ESP_LOGI(TAG, "WiFi scan complete: %zu networks", results.size());
}

void SettingsApp::OnNetworkSelected(const std::string& ssid, bool is_secured) {
    ESP_LOGI(TAG, "Network selected: %s (secured: %d)", ssid.c_str(), is_secured);

    // 检查是否是已连接的网络
    auto* wifi = context_->services().wifi();
    bool is_connected = wifi && (wifi->GetStatus() == WiFiStatus::kConnected);
    std::string saved_ssid = wifi_config_.GetSavedSSID();

    if (is_connected && ssid == saved_ssid) {
        // 已连接的网络：显示详情页面
        ShowPage(SettingsPage::kWiFiDetail);
    } else if (ssid == saved_ssid) {
        // 已保存密码的网络：尝试自动连接
        ESP_LOGI(TAG, "Saved network, attempting auto-connect");
        std::string saved_password;
        if (wifi_config_.LoadCredentials(saved_ssid, saved_password)) {
            ConnectToNetwork(ssid, saved_password);
        } else {
            // 密码读取失败，要求输入
            if (is_secured) {
                ShowPasswordDialog(ssid);
            } else {
                ConnectToNetwork(ssid, "");
            }
        }
    } else if (is_secured) {
        // 加密网络：显示密码输入
        ShowPasswordDialog(ssid);
    } else {
        // 开放网络：直接连接
        ConnectToNetwork(ssid, "");
    }
}

void SettingsApp::ShowPasswordDialog(const std::string& ssid) {
    // 创建模态背景
    password_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(password_dialog_);
    lv_obj_set_size(password_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(password_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(password_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(password_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    // 对话框容器
    auto* dialog_box = lv_obj_create(password_dialog_);
    lv_obj_remove_style_all(dialog_box);
    lv_obj_set_size(dialog_box, 280, 180);
    lv_obj_center(dialog_box);
    lv_obj_set_style_bg_color(dialog_box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog_box, 12, 0);
    lv_obj_set_style_pad_all(dialog_box, 16, 0);
    lv_obj_clear_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    // 标题
    auto* title_label = lv_label_create(dialog_box);
    lv_label_set_text(title_label, "Enter Password");
    lv_obj_set_style_text_color(title_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(title_label, &phone_font_18, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    // SSID 标签
    auto* ssid_label = lv_label_create(dialog_box);
    lv_label_set_text(ssid_label, ssid.c_str());
    lv_obj_set_style_text_color(ssid_label, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_font(ssid_label, &phone_font_12, 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_MID, 0, 26);

    // 密码输入框
    password_textarea_ = lv_textarea_create(dialog_box);
    lv_obj_set_size(password_textarea_, 240, 40);
    lv_obj_align(password_textarea_, LV_ALIGN_TOP_MID, 0, 50);
    lv_textarea_set_placeholder_text(password_textarea_, "Password");
    lv_textarea_set_password_mode(password_textarea_, true);
    lv_textarea_set_one_line(password_textarea_, true);
    lv_obj_set_style_bg_color(password_textarea_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(password_textarea_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_border_color(password_textarea_, rodakos_theme_primary(), LV_STATE_FOCUSED);

    // 显示软键盘
    soft_keyboard_.Show(password_textarea_);

    // 按钮容器
    auto* btn_container = lv_obj_create(dialog_box);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, 240, 40);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);

    // 取消按钮
    auto* cancel_btn = lv_btn_create(btn_container);
    lv_obj_set_size(cancel_btn, 100, 36);
    lv_obj_set_style_bg_color(cancel_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel_btn, 6, 0);

    auto* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_center(cancel_label);

    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        // 隐藏软键盘
        self->soft_keyboard_.Hide();
        // 关闭对话框
        if (self->password_dialog_ != nullptr && lv_obj_is_valid(self->password_dialog_)) {
            lv_obj_delete(self->password_dialog_);
            self->password_dialog_ = nullptr;
            self->password_textarea_ = nullptr;
        }
    }, LV_EVENT_CLICKED, this);

    // 连接按钮
    auto* connect_btn = lv_btn_create(btn_container);
    lv_obj_set_size(connect_btn, 100, 36);
    lv_obj_set_style_bg_color(connect_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(connect_btn, 6, 0);

    auto* connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_set_style_text_color(connect_label, rodakos_theme_bg_primary(), 0);
    lv_obj_center(connect_label);

    // 保存 SSID 到 SettingsApp 成员变量，避免内存管理问题
    connecting_ssid_ = ssid;

    lv_obj_add_event_cb(connect_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));

        const char* password = lv_textarea_get_text(self->password_textarea_);
        self->ConnectToNetwork(self->connecting_ssid_, password ? password : "");

        // 隐藏软键盘
        self->soft_keyboard_.Hide();

        // 关闭对话框
        if (self->password_dialog_ != nullptr && lv_obj_is_valid(self->password_dialog_)) {
            lv_obj_delete(self->password_dialog_);
            self->password_dialog_ = nullptr;
            self->password_textarea_ = nullptr;
        }
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::CreateWiFiDetailPage() {
    wifi_detail_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(wifi_detail_body_);
    lv_obj_set_size(wifi_detail_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(wifi_detail_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(wifi_detail_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(wifi_detail_body_, 0, 0);
    lv_obj_add_flag(wifi_detail_body_, LV_OBJ_FLAG_HIDDEN);

    // 内容区域
    auto* content = lv_obj_create(wifi_detail_body_);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 300, 148);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 12, 0);

    // SSID
    detail_ssid_label_ = lv_label_create(content);
    lv_label_set_text(detail_ssid_label_, "SSID: ");
    lv_obj_set_style_text_color(detail_ssid_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(detail_ssid_label_, &phone_font_14, 0);

    // 状态
    detail_status_label_ = lv_label_create(content);
    lv_label_set_text(detail_status_label_, "Status: ");
    lv_obj_set_style_text_color(detail_status_label_, rodakos_theme_text_secondary(), 0);

    // IP 地址
    detail_ip_label_ = lv_label_create(content);
    lv_label_set_text(detail_ip_label_, "IP Address: ");
    lv_obj_set_style_text_color(detail_ip_label_, rodakos_theme_text_secondary(), 0);

    // 网关
    detail_gateway_label_ = lv_label_create(content);
    lv_label_set_text(detail_gateway_label_, "Gateway: ");
    lv_obj_set_style_text_color(detail_gateway_label_, rodakos_theme_text_secondary(), 0);

    // 子网掩码
    detail_netmask_label_ = lv_label_create(content);
    lv_label_set_text(detail_netmask_label_, "Netmask: ");
    lv_obj_set_style_text_color(detail_netmask_label_, rodakos_theme_text_secondary(), 0);

    // 信号强度
    detail_rssi_label_ = lv_label_create(content);
    lv_label_set_text(detail_rssi_label_, "Signal: ");
    lv_obj_set_style_text_color(detail_rssi_label_, rodakos_theme_text_secondary(), 0);

    // 按钮容器
    auto* btn_container = lv_obj_create(wifi_detail_body_);
    lv_obj_remove_style_all(btn_container);
    lv_obj_set_size(btn_container, 280, 40);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // "断开连接" 按钮
    auto* disconnect_btn = lv_btn_create(btn_container);
    lv_obj_set_size(disconnect_btn, 130, 36);
    lv_obj_set_style_bg_color(disconnect_btn, rodakos_theme_warning(), 0);
    lv_obj_set_style_radius(disconnect_btn, 6, 0);

    auto* disconnect_label = lv_label_create(disconnect_btn);
    lv_label_set_text(disconnect_label, "Disconnect");
    lv_obj_set_style_text_color(disconnect_label, lv_color_white(), 0);
    lv_obj_center(disconnect_label);

    lv_obj_add_event_cb(disconnect_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* wifi = self->context_->services().wifi();
        if (wifi != nullptr) {
            wifi->Disconnect();
            self->ui_->ShowToastUnlocked("Disconnected");
            self->ShowPage(SettingsPage::kWiFiList);
        }
    }, LV_EVENT_CLICKED, this);

    // "忘记网络" 按钮
    auto* forget_btn = lv_btn_create(btn_container);
    lv_obj_set_size(forget_btn, 130, 36);
    lv_obj_set_style_bg_color(forget_btn, rodakos_theme_error(), 0);
    lv_obj_set_style_radius(forget_btn, 6, 0);

    auto* forget_label = lv_label_create(forget_btn);
    lv_label_set_text(forget_label, "Forget");
    lv_obj_set_style_text_color(forget_label, lv_color_white(), 0);
    lv_obj_center(forget_label);

    lv_obj_add_event_cb(forget_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->wifi_config_.ClearCredentials();
        self->ui_->ShowToastUnlocked("Network forgotten");

        auto* wifi = self->context_->services().wifi();
        if (wifi != nullptr) {
            wifi->Disconnect();
        }

        self->ShowPage(SettingsPage::kWiFiList);
    }, LV_EVENT_CLICKED, this);

    UpdateWiFiDetailPage();
}

void SettingsApp::UpdateWiFiDetailPage() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr) {
        return;
    }

    // SSID
    std::string ssid = wifi->GetConnectedSSID();
    char ssid_text[64];
    snprintf(ssid_text, sizeof(ssid_text), "SSID: %s", ssid.c_str());
    lv_label_set_text(detail_ssid_label_, ssid_text);

    // 状态
    WiFiStatus status = wifi->GetStatus();
    const char* status_str = (status == WiFiStatus::kConnected) ? "Connected" : "Disconnected";
    char status_text[64];
    snprintf(status_text, sizeof(status_text), "Status: %s", status_str);
    lv_label_set_text(detail_status_label_, status_text);

    // IP 地址
    std::string ip = wifi->GetIPAddress();
    char ip_text[64];
    snprintf(ip_text, sizeof(ip_text), "IP Address: %s", ip.c_str());
    lv_label_set_text(detail_ip_label_, ip_text);

    // TODO: 从 esp_netif 获取网关和子网掩码
    lv_label_set_text(detail_gateway_label_, "Gateway: 192.168.88.1");
    lv_label_set_text(detail_netmask_label_, "Netmask: 255.255.255.0");

    // TODO: 信号强度需要从 WiFi 驱动获取
    lv_label_set_text(detail_rssi_label_, "Signal: Good");
}

void SettingsApp::ConnectToNetwork(const std::string& ssid, const std::string& password) {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr) {
        ui_->ShowToastUnlocked("WiFi not available");
        return;
    }

    connecting_ssid_ = ssid;

    char status_text[64];
    snprintf(status_text, sizeof(status_text), "Connecting to %s...", ssid.c_str());
    lv_label_set_text(wifi_status_label_, status_text);

    wifi->Connect(ssid, password, [this, ssid, password](WiFiStatus status) {
        // 连接成功后保存凭据
        if (status == WiFiStatus::kConnected) {
            wifi_config_.SaveCredentials(ssid, password);
        }

        lv_async_call([](void* user_data) {
            auto* self = static_cast<SettingsApp*>(user_data);
            // 从连接状态获取结果
            auto* wifi = self->context_->services().wifi();
            if (wifi != nullptr) {
                self->OnConnectResult(wifi->GetStatus(), self->connecting_ssid_);
            }
        }, this);
    });
}

void SettingsApp::OnConnectResult(WiFiStatus status, const std::string& ssid) {
    char msg[128];

    switch (status) {
        case WiFiStatus::kConnected: {
            auto* wifi = context_->services().wifi();
            std::string ip = wifi ? wifi->GetIPAddress() : "unknown";
            snprintf(msg, sizeof(msg), "Connected to %s\nIP: %s", ssid.c_str(), ip.c_str());
            lv_label_set_text(wifi_status_label_, msg);
            ui_->ShowToastUnlocked("WiFi connected!");

            // 保存 WiFi 凭据
            // 注意：密码已经在 ConnectToNetwork 中传递，我们需要保存它
            ESP_LOGI(TAG, "WiFi connected: %s, IP: %s", ssid.c_str(), ip.c_str());
            break;
        }

        case WiFiStatus::kFailed:
            snprintf(msg, sizeof(msg), "Failed to connect to %s", ssid.c_str());
            lv_label_set_text(wifi_status_label_, msg);
            ui_->ShowToastUnlocked("Connection failed");
            ESP_LOGE(TAG, "WiFi connection failed: %s", ssid.c_str());
            break;

        case WiFiStatus::kDisconnected:
            lv_label_set_text(wifi_status_label_, "Disconnected");
            break;

        case WiFiStatus::kConnecting:
            snprintf(msg, sizeof(msg), "Connecting to %s...", ssid.c_str());
            lv_label_set_text(wifi_status_label_, msg);
            break;
    }
}

void RegisterSettingsApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "settings",
        .title = "Settings",
        .icon = FONT_AWESOME_GEAR,
        .category = PhoneAppCategory::kSystem,
        .launch_mode = PhoneAppLaunchMode::kReplaceCurrent,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = true,
        .aliases = {"config", "preferences", "设置"},
        .create = []() { return std::make_unique<SettingsApp>(); },
    });
}
