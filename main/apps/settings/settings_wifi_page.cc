#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <cstdio>
#include <memory>
#include <string>

namespace {
constexpr const char* TAG = "SettingsApp";

struct WiFiScanPayload {
    std::shared_ptr<SettingsWiFiAsyncGuard> guard;
    uint32_t generation = 0;
    std::vector<WiFiScanResult> results;
};

struct WiFiConnectPayload {
    std::shared_ptr<SettingsWiFiAsyncGuard> guard;
    uint32_t generation = 0;
    WiFiStatus status = WiFiStatus::kDisconnected;
    std::string ssid;
    std::string password;
};

struct NetworkItemPayload {
    size_t index = 0;
};

void NetworkItemDeleteEvent(lv_event_t* e) {
    auto* payload = static_cast<NetworkItemPayload*>(lv_event_get_user_data(e));
    delete payload;
}

void WiFiScanCompleteCallback(void* user_data) {
    auto* payload = static_cast<WiFiScanPayload*>(user_data);
    if (payload == nullptr) {
        return;
    }
    auto guard = payload->guard;
    auto* app = guard ? guard->app.load() : nullptr;
    if (app != nullptr && payload->generation == guard->generation.load()) {
        app->OnWiFiScanAsyncComplete(payload->results);
    }
    delete payload;
}

void WiFiConnectCompleteCallback(void* user_data) {
    auto* payload = static_cast<WiFiConnectPayload*>(user_data);
    if (payload == nullptr) {
        return;
    }
    auto guard = payload->guard;
    auto* app = guard ? guard->app.load() : nullptr;
    if (app != nullptr && payload->generation == guard->generation.load()) {
        app->OnWiFiConnectAsyncComplete(payload->status, payload->ssid, payload->password);
    }
    delete payload;
}

template <typename Payload, typename Callback>
void QueueWiFiAsyncPayload(Payload* payload, Callback callback) {
    bool queued = false;
    if (lvgl_port_lock(1000)) {
        queued = lv_async_call(callback, payload) == LV_RESULT_OK;
        lvgl_port_unlock();
    }
    if (!queued) {
        delete payload;
    }
}

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

    if (is_connected) {
        lv_obj_set_style_border_width(item, 2, 0);
        lv_obj_set_style_border_color(item, rodakos_theme_primary(), 0);
    }

    lv_obj_set_style_bg_color(item, rodakos_theme_primary(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_20, LV_STATE_PRESSED);

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

    const char* signal_icon;
    lv_color_t signal_color;
    if (ap.rssi >= -50) {
        signal_icon = FONT_AWESOME_SIGNAL_STRONG;
        signal_color = rodakos_theme_success();
    } else if (ap.rssi >= -70) {
        signal_icon = FONT_AWESOME_SIGNAL_GOOD;
        signal_color = rodakos_theme_primary();
    } else if (ap.rssi >= -80) {
        signal_icon = FONT_AWESOME_SIGNAL_FAIR;
        signal_color = rodakos_theme_warning();
    } else {
        signal_icon = FONT_AWESOME_SIGNAL_WEAK;
        signal_color = rodakos_theme_error();
    }

    auto* signal_label = lv_label_create(item);
    lv_label_set_text(signal_label, signal_icon);
    lv_obj_set_style_text_color(signal_label, signal_color, 0);
    lv_obj_set_style_text_font(signal_label, PhoneIconFont(), 0);
    lv_obj_align(signal_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    char info_text[48];
    if (ap.is_secured) {
        snprintf(info_text, sizeof(info_text), FONT_AWESOME_LOCK " Secured • %d dBm", ap.rssi);
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

using namespace rodakos_settings;
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

void SettingsApp::StartWiFiScan() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr) {
        ui_->ShowToastUnlocked("WiFi not available");
        return;
    }

    lv_label_set_text(wifi_status_label_, "Scanning...");
    lv_obj_clean(wifi_list_container_);

    auto guard = wifi_async_guard_;
    const uint32_t generation = guard != nullptr ? guard->generation.load() : 0;
    wifi->StartScan([guard, generation](const std::vector<WiFiScanResult>& results) {
        auto* payload = new WiFiScanPayload{
            .guard = guard,
            .generation = generation,
            .results = results,
        };
        QueueWiFiAsyncPayload(payload, WiFiScanCompleteCallback);
    });
}

void SettingsApp::OnWiFiScanAsyncComplete(const std::vector<WiFiScanResult>& results) {
    wifi_scan_results_ = results;
    OnWiFiScanComplete(wifi_scan_results_);
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
        auto* payload = new NetworkItemPayload{.index = i};
        lv_obj_set_user_data(item, payload);
        lv_obj_add_event_cb(item, NetworkItemDeleteEvent, LV_EVENT_DELETE, payload);

        lv_obj_add_event_cb(item, [](lv_event_t* e) {
            auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
            auto* item = static_cast<lv_obj_t*>(lv_event_get_target(e));
            auto* payload = static_cast<NetworkItemPayload*>(lv_obj_get_user_data(item));
            const size_t index = payload != nullptr ? payload->index : self->wifi_scan_results_.size();

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

    auto guard = wifi_async_guard_;
    const uint32_t generation = guard != nullptr ? guard->generation.load() : 0;
    wifi->Connect(ssid, password, [guard, generation, ssid, password](WiFiStatus status) {
        auto* payload = new WiFiConnectPayload{
            .guard = guard,
            .generation = generation,
            .status = status,
            .ssid = ssid,
            .password = password,
        };
        QueueWiFiAsyncPayload(payload, WiFiConnectCompleteCallback);
    });
}

void SettingsApp::OnWiFiConnectAsyncComplete(WiFiStatus status,
                                             const std::string& ssid,
                                             const std::string& password) {
    if (status == WiFiStatus::kConnected) {
        wifi_config_.SaveCredentials(ssid, password);
    }
    OnConnectResult(status, ssid);
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
