#include "apps/settings/settings_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_layout.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/backlight_adapter.h"
#include "settings.h"

#include <esp_log.h>
#include <cstdio>

namespace {
constexpr const char* TAG = "SettingsApp";
constexpr const char* kDisplayNamespace = "display";
constexpr const char* kBrightnessKey = "brightness";
constexpr const char* kThemeKey = "theme";
constexpr const char* kLanguageKey = "language";

void UpdateBrightnessLabel(lv_obj_t* label, int value) {
    if (label != nullptr) {
        lv_label_set_text_fmt(label, "%d%%", value);
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
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
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
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // 信号强度条（右上角）
    const char* signal_icon;
    lv_color_t signal_color;
    if (ap.rssi >= -50) {
        signal_icon = "▂▄▆█";  // 强信号
        signal_color = rodakos_theme_success();
    } else if (ap.rssi >= -70) {
        signal_icon = "▂▄▆";    // 中等信号
        signal_color = rodakos_theme_primary();
    } else if (ap.rssi >= -80) {
        signal_icon = "▂▄";      // 弱信号
        signal_color = rodakos_theme_warning();
    } else {
        signal_icon = "▂";        // 很弱
        signal_color = rodakos_theme_error();
    }

    auto* signal_label = lv_label_create(item);
    lv_label_set_text(signal_label, signal_icon);
    lv_obj_set_style_text_color(signal_label, signal_color, 0);
    lv_obj_set_style_text_font(signal_label, &lv_font_montserrat_12, 0);
    lv_obj_align(signal_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    // 底部信息：加密状态 + 信号强度数值
    char info_text[48];
    if (ap.is_secured) {
        snprintf(info_text, sizeof(info_text), "\xF0\x9F\x94\x92 Secured • %d dBm", ap.rssi);  // 🔒 emoji
    } else {
        snprintf(info_text, sizeof(info_text), "Open • %d dBm", ap.rssi);
    }

    auto* info_label = lv_label_create(item);
    lv_label_set_text(info_label, info_text);
    lv_obj_set_style_text_color(info_label, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
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

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    // 从设置中加载并应用主题
    Settings display_settings(kDisplayNamespace, false);
    const std::string theme = display_settings.GetString(kThemeKey, "dark");

    rodakos_theme_preset_t preset = RODAKOS_THEME_DARK;
    if (theme == "light") preset = RODAKOS_THEME_LIGHT;
    else if (theme == "blue") preset = RODAKOS_THEME_BLUE;
    else if (theme == "green") preset = RODAKOS_THEME_GREEN;
    rodakos_theme_init(preset);

    // 初始化布局系统
    rodakos_layout_init(nullptr);

    // 创建根容器
    lv_obj_t* header = nullptr;
    lv_obj_t* body = nullptr;
    lv_obj_t* footer = nullptr;
    root_ = rodakos_layout_create(ui_->screen(), &header, &body, &footer);

    // ===== HEADER 区域（固定） =====
    auto* title_label = lv_label_create(header);
    lv_label_set_text(title_label, "Settings");
    lv_obj_set_style_text_color(title_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
    lv_obj_center(title_label);

    // 返回按钮
    auto* back_btn = lv_btn_create(header);
    lv_obj_remove_style_all(back_btn);
    lv_obj_set_size(back_btn, 40, 24);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, rodakos_layout_padding_medium(), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);

    auto* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "<");
    lv_obj_set_style_text_color(back_label, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_18, 0);
    lv_obj_center(back_label);

    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        if (self->current_page_ == SettingsPage::kWiFiList) {
            // WiFi 页面返回主设置页面
            self->ShowPage(SettingsPage::kMain);
        } else {
            // 主页面返回 Home
            self->context_->navigation().ReturnHome();
        }
    }, LV_EVENT_CLICKED, this);

    // 创建主设置页面
    main_body_ = body;
    CreateMainPage();

    ESP_LOGI(TAG, "Settings app created");
    return true;
}

void SettingsApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }
    root_ = nullptr;
    main_body_ = nullptr;
    wifi_body_ = nullptr;
    wifi_detail_body_ = nullptr;
    brightness_label_ = nullptr;
    brightness_slider_ = nullptr;
    theme_dropdown_ = nullptr;
    language_switch_ = nullptr;
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

    // 显示目标页面
    switch (page) {
        case SettingsPage::kMain:
            if (main_body_ != nullptr) {
                lv_obj_clear_flag(main_body_, LV_OBJ_FLAG_HIDDEN);
            }
            break;

        case SettingsPage::kWiFiList:
            if (wifi_body_ == nullptr) {
                CreateWiFiListPage();
            }
            lv_obj_clear_flag(wifi_body_, LV_OBJ_FLAG_HIDDEN);
            StartWiFiScan();  // 自动扫描
            break;

        case SettingsPage::kWiFiDetail:
            if (wifi_detail_body_ == nullptr) {
                CreateWiFiDetailPage();
            }
            lv_obj_clear_flag(wifi_detail_body_, LV_OBJ_FLAG_HIDDEN);
            UpdateWiFiDetailPage();  // 刷新数据
            break;
    }
}

void SettingsApp::CreateMainPage() {
    Settings display_settings(kDisplayNamespace, false);
    const int brightness = display_settings.GetInt(kBrightnessKey, 75);
    const std::string theme = display_settings.GetString(kThemeKey, "dark");
    const std::string language = display_settings.GetString(kLanguageKey, "en");

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

    auto* brightness_title = CreateSettingLabel(brightness_card, "Brightness");
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 0, 0);

    brightness_label_ = CreateSettingLabel(brightness_card, "", true);
    lv_obj_align(brightness_label_, LV_ALIGN_TOP_RIGHT, 0, 0);
    UpdateBrightnessLabel(brightness_label_, brightness);

    brightness_slider_ = lv_slider_create(brightness_card);
    lv_obj_set_size(brightness_slider_, 270, 8);
    lv_obj_align(brightness_slider_, LV_ALIGN_BOTTOM_MID, 0, -4);
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
    auto* theme_title = CreateSettingLabel(theme_card, "Theme");
    lv_obj_align(theme_title, LV_ALIGN_LEFT_MID, 0, 0);

    theme_dropdown_ = lv_dropdown_create(theme_card);
    lv_dropdown_set_options(theme_dropdown_, "Dark\nLight\nBlue\nGreen");
    lv_obj_set_width(theme_dropdown_, 100);
    lv_obj_align(theme_dropdown_, LV_ALIGN_RIGHT_MID, 0, 0);

    if (theme == "light") lv_dropdown_set_selected(theme_dropdown_, 1);
    else if (theme == "blue") lv_dropdown_set_selected(theme_dropdown_, 2);
    else if (theme == "green") lv_dropdown_set_selected(theme_dropdown_, 3);
    else lv_dropdown_set_selected(theme_dropdown_, 0);

    lv_obj_set_style_bg_color(theme_dropdown_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(theme_dropdown_, rodakos_theme_text_primary(), 0);

    lv_obj_add_event_cb(theme_dropdown_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const uint16_t selected = lv_dropdown_get_selected(dd);

        const char* theme_names[] = {"dark", "light", "blue", "green"};
        const rodakos_theme_preset_t presets[] = {
            RODAKOS_THEME_DARK, RODAKOS_THEME_LIGHT,
            RODAKOS_THEME_BLUE, RODAKOS_THEME_GREEN
        };

        if (selected < 4) {
            Settings settings(kDisplayNamespace, true);
            settings.SetString(kThemeKey, theme_names[selected]);
            rodakos_theme_init(presets[selected]);
            self->ui_->ShowToastUnlocked("Theme changed, reloading...");
            self->context_->navigation().Launch("settings");
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    // ===== 语言设置卡片 =====
    auto* language_card = CreateSettingCard(main_body_, 142);
    auto* language_title = CreateSettingLabel(language_card, "Chinese language");
    lv_obj_align(language_title, LV_ALIGN_LEFT_MID, 0, 0);

    language_switch_ = lv_switch_create(language_card);
    lv_obj_align(language_switch_, LV_ALIGN_RIGHT_MID, 0, 0);
    if (language == "zh") {
        lv_obj_add_state(language_switch_, LV_STATE_CHECKED);
    }

    lv_obj_set_style_bg_color(language_switch_, rodakos_theme_success(), LV_PART_INDICATOR | LV_STATE_CHECKED);
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

    auto* wifi_title = CreateSettingLabel(wifi_card, "WiFi Settings");
    lv_obj_align(wifi_title, LV_ALIGN_LEFT_MID, 0, 0);

    auto* wifi_arrow = lv_label_create(wifi_card);
    lv_label_set_text(wifi_arrow, ">");
    lv_obj_set_style_text_color(wifi_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(wifi_arrow, &lv_font_montserrat_18, 0);
    lv_obj_align(wifi_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(wifi_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kWiFiList);
    }, LV_EVENT_CLICKED, this);
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
    lv_label_set_text(status_icon, "📶");
    lv_obj_set_style_text_font(status_icon, &lv_font_montserrat_18, 0);
    lv_obj_align(status_icon, LV_ALIGN_LEFT_MID, 0, 0);

    // 状态文字
    wifi_status_label_ = lv_label_create(info_container);
    lv_label_set_text(wifi_status_label_, "Tap 'Scan' to search");
    lv_obj_set_width(wifi_status_label_, 220);
    lv_label_set_long_mode(wifi_status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(wifi_status_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(wifi_status_label_, &lv_font_montserrat_12, 0);
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
    lv_obj_set_style_text_font(scan_label, &lv_font_montserrat_12, 0);
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
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -5);
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
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    // SSID 标签
    auto* ssid_label = lv_label_create(dialog_box);
    lv_label_set_text(ssid_label, ssid.c_str());
    lv_obj_set_style_text_color(ssid_label, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_12, 0);
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
    wifi_detail_body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(wifi_detail_body_);
    lv_obj_set_size(wifi_detail_body_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_detail_body_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(wifi_detail_body_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(wifi_detail_body_, 0, 0);

    // 标题栏
    auto* title_bar = lv_obj_create(wifi_detail_body_);
    lv_obj_remove_style_all(title_bar);
    lv_obj_set_size(title_bar, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(title_bar, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(title_bar, 12, 0);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

    // 返回按钮
    auto* back_btn = lv_btn_create(title_bar);
    lv_obj_set_size(back_btn, 60, 32);
    lv_obj_set_style_bg_color(back_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);

    auto* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, rodakos_theme_text_primary(), 0);
    lv_obj_center(back_label);

    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kWiFiList);
    }, LV_EVENT_CLICKED, this);

    // 标题
    auto* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "WiFi Details");
    lv_obj_set_style_text_color(title_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

    // 内容区域
    auto* content = lv_obj_create(wifi_detail_body_);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 300, 196);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 12, 0);

    // SSID
    detail_ssid_label_ = lv_label_create(content);
    lv_label_set_text(detail_ssid_label_, "SSID: ");
    lv_obj_set_style_text_color(detail_ssid_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(detail_ssid_label_, &lv_font_montserrat_14, 0);

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
        .icon = "S",
        .category = PhoneAppCategory::kSystem,
        .launch_mode = PhoneAppLaunchMode::kReplaceCurrent,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = true,
        .aliases = {"config", "preferences", "设置"},
        .create = []() { return std::make_unique<SettingsApp>(); },
    });
}
