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

// Wi-Fi list, password entry and details. Layout follows the Settings grid: x=10, width 300,
// 38 px rows, actions at the bottom of the 320x200 body.
namespace {
constexpr const char* TAG = "SettingsApp";

constexpr lv_coord_t kGridX = 10;
constexpr lv_coord_t kGridWidth = 300;
constexpr lv_coord_t kRowHeight = 38;
constexpr lv_coord_t kActionY = 150;
constexpr lv_coord_t kActionHeight = 40;

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

// Words instead of colour alone, so the strength reads without telling hues apart.
const char* SignalWord(int rssi) {
    if (rssi >= -55) return "强";
    if (rssi >= -67) return "中";
    if (rssi >= -78) return "较弱";
    return "弱";
}

lv_obj_t* CreateGroupList(lv_obj_t* parent, lv_coord_t y, int rows) {
    auto* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, kGridWidth, kRowHeight * rows);
    lv_obj_set_pos(list, kGridX, y);
    lv_obj_set_style_bg_color(list, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_clip_corner(list, true, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    return list;
}

// One network row: name on the left, "🔒 中" or status on the right.
lv_obj_t* CreateNetworkRow(lv_obj_t* list, int index, const std::string& ssid,
                           const std::string& value, lv_color_t value_color, bool chevron) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, kGridWidth, kRowHeight);
    lv_obj_set_pos(row, 0, kRowHeight * index);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    if (index > 0) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, rodakos_theme_bg_tertiary(), 0);
    }

    auto* icon = lv_label_create(row);
    lv_label_set_text(icon, ICON_WIFI);
    lv_obj_set_style_text_color(icon, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(icon, PhoneIconFont(), 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

    auto* name = rodakos_settings::CreateSettingLabel(row, ssid.c_str(), false);
    lv_obj_set_width(name, 150);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 28, 0);

    auto* status = rodakos_settings::CreateSettingLabel(row, value.c_str(), true);
    lv_obj_set_width(status, 90);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(status, &phone_font_12, 0);
    lv_obj_set_style_text_color(status, value_color, 0);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, chevron ? -18 : 0, 0);

    if (chevron) {
        auto* arrow = lv_label_create(row);
        lv_label_set_text(arrow, ">");
        lv_obj_set_style_text_color(arrow, rodakos_theme_text_tertiary(), 0);
        lv_obj_set_style_text_font(arrow, &phone_font_14, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    return row;
}

lv_obj_t* CreateWideButton(lv_obj_t* parent, const char* text, bool primary, bool danger,
                           lv_coord_t x, lv_coord_t width) {
    auto* button = lv_btn_create(parent);
    lv_obj_set_size(button, width, kActionHeight);
    lv_obj_set_pos(button, x, kActionY);
    lv_obj_set_style_radius(button, 7, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, danger ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, primary ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_border_width(button, danger ? 1 : 0, 0);
    lv_obj_set_style_border_color(button, rodakos_theme_error(), 0);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    lv_obj_set_style_text_color(label,
                                danger ? rodakos_theme_error()
                                       : (primary ? lv_color_white() : rodakos_theme_text_primary()),
                                0);
    lv_obj_center(label);
    return button;
}

lv_obj_t* CreateDetailRow(lv_obj_t* list, int index, const char* name) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, kGridWidth, 26);
    lv_obj_set_pos(row, 0, 26 * index + 4);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    auto* key = rodakos_settings::CreateSettingLabel(row, name, true);
    lv_obj_set_style_text_font(key, &phone_font_12, 0);
    lv_obj_align(key, LV_ALIGN_LEFT_MID, 0, 0);
    auto* value = rodakos_settings::CreateSettingLabel(row, "", false);
    lv_obj_set_width(value, 190);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(value, &phone_font_12, 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
    return value;
}
}  // namespace

using namespace rodakos_settings;

void SettingsApp::CreateWiFiListPage() {
    wifi_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(wifi_body_);
    lv_obj_set_size(wifi_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(wifi_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(wifi_body_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(wifi_body_, LV_OBJ_FLAG_HIDDEN);

    // The whole list scrolls; the status line and rescan button sit at its top.
    wifi_list_container_ = lv_obj_create(wifi_body_);
    lv_obj_remove_style_all(wifi_list_container_);
    lv_obj_set_size(wifi_list_container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_bottom(wifi_list_container_, 8, 0);
    lv_obj_set_scrollbar_mode(wifi_list_container_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(wifi_list_container_, LV_DIR_VER);

    wifi_status_label_ = CreateSettingLabel(wifi_body_, "", true);
    lv_obj_set_width(wifi_status_label_, 200);
    lv_label_set_long_mode(wifi_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(wifi_status_label_, &phone_font_12, 0);
    lv_obj_set_pos(wifi_status_label_, kGridX + 4, 8);

    auto* scan_btn = lv_btn_create(wifi_body_);
    lv_obj_set_size(scan_btn, 84, 30);
    lv_obj_set_pos(scan_btn, kGridX + kGridWidth - 84, 2);
    lv_obj_set_style_bg_color(scan_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(scan_btn, 7, 0);
    lv_obj_set_style_shadow_width(scan_btn, 0, 0);
    lv_obj_set_ext_click_area(scan_btn, 6);
    auto* scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "重新扫描");
    lv_obj_set_style_text_color(scan_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(scan_label, &phone_font_12, 0);
    lv_obj_center(scan_label);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->StartWiFiScan();
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::StartWiFiScan() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr) {
        lv_label_set_text(wifi_status_label_, "Wi-Fi 不可用");
        return;
    }

    lv_label_set_text(wifi_status_label_, "正在扫描…");
    auto guard = wifi_async_guard_;
    const uint32_t generation = guard != nullptr ? guard->generation.load() : 0;
    const bool started = wifi->StartScan([guard, generation](const std::vector<WiFiScanResult>& results) {
        auto* payload = new WiFiScanPayload{
            .guard = guard,
            .generation = generation,
            .results = results,
        };
        QueueWiFiAsyncPayload(payload, WiFiScanCompleteCallback);
    });
    if (!started) {
        // Keep the previous results visible; the user can tap 重新扫描 again.
        lv_label_set_text(wifi_status_label_, "扫描没有开始，请稍后重试");
    }
}

void SettingsApp::OnWiFiScanAsyncComplete(const std::vector<WiFiScanResult>& results) {
    wifi_scan_results_ = results;
    OnWiFiScanComplete(wifi_scan_results_);
}

void SettingsApp::OnWiFiScanComplete(const std::vector<WiFiScanResult>& results) {
    if (wifi_list_container_ == nullptr) return;
    lv_obj_clean(wifi_list_container_);

    auto* wifi = context_->services().wifi();
    const bool connected = wifi != nullptr && wifi->GetStatus() == WiFiStatus::kConnected;
    const std::string current_ssid = connected ? wifi->GetConnectedSSID() : std::string();
    const std::string saved_ssid = wifi_config_.GetSavedSSID();

    // Rows under the 38 px status/rescan line.
    lv_coord_t y = 38;

    // Current network first; the info icon opens details. A saved but unreachable network also gets a row,
    // so the user can still forget it or re-enter its password.
    const std::string pinned_ssid = !current_ssid.empty() ? current_ssid : saved_ssid;
    if (!pinned_ssid.empty()) {
        auto* list = CreateGroupList(wifi_list_container_, y, 1);
        const bool is_current = !current_ssid.empty();
        auto* row = CreateNetworkRow(list, 0, pinned_ssid, is_current ? "已连接  " ICON_INFO : "已保存  " ICON_INFO,
                                     is_current ? rodakos_theme_success() : rodakos_theme_text_secondary(),
                                     false);
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowPage(SettingsPage::kWiFiDetail);
        }, LV_EVENT_CLICKED, this);
        y += kRowHeight + 8;
    }

    std::vector<size_t> others;
    for (size_t index = 0; index < results.size(); ++index) {
        if (!results[index].ssid.empty() && results[index].ssid != pinned_ssid) others.push_back(index);
    }

    if (!others.empty()) {
        auto* label = CreateSettingLabel(wifi_list_container_, "其他网络", true);
        lv_obj_set_style_text_font(label, &phone_font_12, 0);
        lv_obj_set_pos(label, kGridX + 4, y);
        y += 18;
        auto* list = CreateGroupList(wifi_list_container_, y, static_cast<int>(others.size()));
        for (size_t row_index = 0; row_index < others.size(); ++row_index) {
            const auto& ap = results[others[row_index]];
            const std::string value = std::string(ap.is_secured ? ICON_LOCK " " : "开放 · ") + SignalWord(ap.rssi);
            auto* row = CreateNetworkRow(list, static_cast<int>(row_index), ap.ssid, value,
                                         rodakos_theme_text_secondary(), false);
            auto* payload = new NetworkItemPayload{others[row_index]};
            lv_obj_set_user_data(row, payload);
            lv_obj_add_event_cb(row, NetworkItemDeleteEvent, LV_EVENT_DELETE, payload);
            lv_obj_add_event_cb(row, [](lv_event_t* e) {
                auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                auto* item = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                auto* payload = static_cast<NetworkItemPayload*>(lv_obj_get_user_data(item));
                if (payload != nullptr && payload->index < self->wifi_scan_results_.size()) {
                    const auto& ap = self->wifi_scan_results_[payload->index];
                    self->OnNetworkSelected(ap.ssid, ap.is_secured);
                }
            }, LV_EVENT_CLICKED, this);
        }
    }

    char status[48];
    std::snprintf(status, sizeof(status), results.empty() ? "没有找到网络" : "找到 %u 个网络",
                  static_cast<unsigned>(results.size()));
    lv_label_set_text(wifi_status_label_, status);
    ESP_LOGI(TAG, "WiFi scan complete: %zu networks", results.size());
}

void SettingsApp::OnNetworkSelected(const std::string& ssid, bool is_secured) {
    ESP_LOGI(TAG, "Network selected: %s (secured: %d)", ssid.c_str(), is_secured);
    std::string saved_ssid = wifi_config_.GetSavedSSID();
    if (ssid == saved_ssid) {
        std::string saved_password;
        if (wifi_config_.LoadCredentials(saved_ssid, saved_password)) {
            ConnectToNetwork(ssid, saved_password);
            return;
        }
    }
    if (is_secured) {
        ShowPasswordDialog(ssid);
    } else {
        ConnectToNetwork(ssid, "");
    }
}

void SettingsApp::ClosePasswordDialog() {
    soft_keyboard_.Hide();
    if (password_dialog_ != nullptr && lv_obj_is_valid(password_dialog_)) {
        lv_obj_delete(password_dialog_);
    }
    password_dialog_ = nullptr;
    password_textarea_ = nullptr;
}

void SettingsApp::ShowPasswordDialog(const std::string& ssid, const char* notice) {
    ClosePasswordDialog();
    connecting_ssid_ = ssid;

    // Full-screen layer: the 120 px keyboard takes the bottom half, the field sits above it.
    password_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(password_dialog_);
    lv_obj_set_size(password_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(password_dialog_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(password_dialog_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(password_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* box = lv_obj_create(password_dialog_);
    lv_obj_remove_style_all(box);
    // Ends at y=116, above the keyboard (y=120).
    lv_obj_set_size(box, 304, 108);
    lv_obj_set_pos(box, 8, 8);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_hor(box, 12, 0);
    lv_obj_set_style_pad_ver(box, 8, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    auto* caption = CreateSettingLabel(box, notice != nullptr ? notice : "连接到", notice != nullptr ? false : true);
    lv_obj_set_width(caption, 236);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(caption, &phone_font_12, 0);
    if (notice != nullptr) lv_obj_set_style_text_color(caption, rodakos_theme_warning(), 0);
    lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 0, 0);

    auto* name = CreateSettingLabel(box, ssid.c_str(), false);
    lv_obj_set_width(name, 236);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 18);

    password_textarea_ = lv_textarea_create(box);
    lv_obj_set_size(password_textarea_, 160, 36);
    lv_obj_align(password_textarea_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_textarea_set_placeholder_text(password_textarea_, "密码");
    lv_textarea_set_password_mode(password_textarea_, true);
    lv_textarea_set_one_line(password_textarea_, true);
    lv_textarea_set_max_length(password_textarea_, 64);
    lv_obj_set_style_bg_color(password_textarea_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(password_textarea_, rodakos_theme_text_primary(), 0);
    // LVGL's default font has no CJK fallback; the 密码 placeholder would draw as boxes.
    lv_obj_set_style_text_font(password_textarea_, &phone_font_14, 0);
    lv_obj_set_style_border_width(password_textarea_, 0, 0);
    lv_obj_set_style_radius(password_textarea_, 6, 0);

    // Long passwords are easy to mistype on a small keyboard; let the user check them.
    auto* reveal = lv_btn_create(box);
    lv_obj_set_size(reveal, 48, 36);
    lv_obj_align(reveal, LV_ALIGN_BOTTOM_LEFT, 164, 0);
    lv_obj_set_style_bg_color(reveal, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(reveal, 6, 0);
    lv_obj_set_style_shadow_width(reveal, 0, 0);
    auto* reveal_label = lv_label_create(reveal);
    lv_label_set_text(reveal_label, "显示");
    lv_obj_set_style_text_color(reveal_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(reveal_label, &phone_font_12, 0);
    lv_obj_center(reveal_label);
    lv_obj_add_event_cb(reveal, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* button = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        if (self->password_textarea_ == nullptr) return;
        const bool hidden = lv_textarea_get_password_mode(self->password_textarea_);
        lv_textarea_set_password_mode(self->password_textarea_, !hidden);
        lv_label_set_text(lv_obj_get_child(button, 0), hidden ? "隐藏" : "显示");
    }, LV_EVENT_CLICKED, this);

    // 取消 / 连接 share the field row; the keyboard's OK key also connects.
    auto* cancel = lv_btn_create(box);
    lv_obj_set_size(cancel, 36, 36);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(cancel, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel, 5, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    auto* cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, ICON_CLOSE);
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cancel_label, PhoneIconFont(), 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ClosePasswordDialog();
    }, LV_EVENT_CLICKED, this);

    auto* connect = lv_btn_create(box);
    lv_obj_set_size(connect, 64, 36);
    lv_obj_align(connect, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(connect, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(connect, 5, 0);
    lv_obj_set_style_shadow_width(connect, 0, 0);
    auto* connect_label = lv_label_create(connect);
    lv_label_set_text(connect_label, "连接");
    lv_obj_set_style_text_color(connect_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(connect_label, &phone_font_14, 0);
    lv_obj_center(connect_label);
    const auto submit = [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->SubmitPasswordDialog();
    };
    lv_obj_add_event_cb(connect, submit, LV_EVENT_CLICKED, this);

    soft_keyboard_.Show(password_textarea_, [this]() {
        // Runs from the keyboard event; defer the teardown until after the event returns.
        lv_async_call([](void* user_data) {
            static_cast<SettingsApp*>(user_data)->SubmitPasswordDialog();
        }, this);
    });
}

void SettingsApp::SubmitPasswordDialog() {
    if (password_textarea_ == nullptr) return;
    const char* text = lv_textarea_get_text(password_textarea_);
    const std::string password = text != nullptr ? text : "";
    const std::string ssid = connecting_ssid_;
    ClosePasswordDialog();
    ConnectToNetwork(ssid, password);
}

void SettingsApp::CreateWiFiDetailPage() {
    wifi_detail_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(wifi_detail_body_);
    lv_obj_set_size(wifi_detail_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(wifi_detail_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(wifi_detail_body_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(wifi_detail_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_detail_body_, LV_OBJ_FLAG_HIDDEN);

    auto* card = lv_obj_create(wifi_detail_body_);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, kGridWidth, 136);
    lv_obj_set_pos(card, kGridX, 6);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    detail_ssid_label_ = CreateDetailRow(card, 0, "网络");
    detail_status_label_ = CreateDetailRow(card, 1, "状态");
    detail_ip_label_ = CreateDetailRow(card, 2, "IP 地址");
    detail_gateway_label_ = CreateDetailRow(card, 3, "网关 / 掩码");
    detail_rssi_label_ = CreateDetailRow(card, 4, "信号");
    detail_netmask_label_ = nullptr;

    wifi_detail_connect_button_ = CreateWideButton(wifi_detail_body_, "重新输入密码", false, false,
                                                   kGridX, (kGridWidth - 8) / 2);
    lv_obj_add_event_cb(wifi_detail_connect_button_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        const std::string ssid = self->wifi_config_.GetSavedSSID();
        if (!ssid.empty()) self->ShowPasswordDialog(ssid);
    }, LV_EVENT_CLICKED, this);

    auto* forget = CreateWideButton(wifi_detail_body_, "忘记此网络", false, true,
                                    kGridX + (kGridWidth - 8) / 2 + 8, (kGridWidth - 8) / 2);
    lv_obj_add_event_cb(forget, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowForgetWiFiDialog();
    }, LV_EVENT_CLICKED, this);

    UpdateWiFiDetailPage();
}

void SettingsApp::ShowForgetWiFiDialog() {
    const std::string ssid = wifi_config_.GetSavedSSID();
    if (ssid.empty() || wifi_forget_dialog_ != nullptr) return;

    wifi_forget_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(wifi_forget_dialog_);
    lv_obj_set_size(wifi_forget_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(wifi_forget_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wifi_forget_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(wifi_forget_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* box = lv_obj_create(wifi_forget_dialog_);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 296, 150);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 14, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    const std::string title_text = "忘记「" + ssid + "」？";
    auto* title = CreateSettingLabel(box, title_text.c_str(), false);
    lv_obj_set_width(title, 268);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    auto* message = CreateSettingLabel(box, "设备会断开这个网络并删除保存的密码，下次需要重新输入。", true);
    lv_obj_set_width(message, 268);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(message, &phone_font_12, 0);
    lv_obj_align(message, LV_ALIGN_TOP_LEFT, 0, 30);

    auto* cancel = lv_btn_create(box);
    lv_obj_set_size(cancel, 128, 36);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel, 7, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    auto* cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cancel_label, &phone_font_14, 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->CloseForgetWiFiDialog();
    }, LV_EVENT_CLICKED, this);

    auto* confirm = lv_btn_create(box);
    lv_obj_set_size(confirm, 128, 36);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(confirm, rodakos_theme_error(), 0);
    lv_obj_set_style_radius(confirm, 7, 0);
    lv_obj_set_style_shadow_width(confirm, 0, 0);
    auto* confirm_label = lv_label_create(confirm);
    lv_label_set_text(confirm_label, "忘记");
    lv_obj_set_style_text_color(confirm_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(confirm_label, &phone_font_14, 0);
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->CloseForgetWiFiDialog();
        if (!self->wifi_config_.ClearCredentials()) {
            self->ui_->ShowToastUnlocked("删除失败，请重试");
            return;
        }
        if (auto* wifi = self->context_->services().wifi(); wifi != nullptr) wifi->Disconnect();
        self->ui_->ShowToastUnlocked("已忘记该网络");
        self->ShowPage(SettingsPage::kWiFiList);
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::CloseForgetWiFiDialog() {
    if (wifi_forget_dialog_ != nullptr && lv_obj_is_valid(wifi_forget_dialog_)) {
        lv_obj_delete(wifi_forget_dialog_);
    }
    wifi_forget_dialog_ = nullptr;
}

void SettingsApp::UpdateWiFiDetailPage() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr || detail_ssid_label_ == nullptr) {
        return;
    }
    const WiFiStatus status = wifi->GetStatus();
    const bool connected = status == WiFiStatus::kConnected;
    const std::string saved = wifi_config_.GetSavedSSID();
    const std::string ssid = connected ? wifi->GetConnectedSSID() : saved;
    lv_label_set_text(detail_ssid_label_, ssid.empty() ? "—" : ssid.c_str());

    const char* status_text = connected ? "已连接"
        : status == WiFiStatus::kConnecting ? "正在连接…"
        : wifi->LastFailureWasAuthRejected() ? "密码错误"
        : "未连接";
    lv_label_set_text(detail_status_label_, status_text);
    lv_obj_set_style_text_color(detail_status_label_,
                                connected ? rodakos_theme_success()
                                : wifi->LastFailureWasAuthRejected() ? rodakos_theme_error()
                                : rodakos_theme_text_primary(),
                                0);

    const std::string ip = connected ? wifi->GetIPAddress() : std::string();
    lv_label_set_text(detail_ip_label_, ip.empty() ? "—" : ip.c_str());

    const WiFiLinkInfo link = wifi->GetLinkInfo();
    if (link.valid) {
        const std::string net = link.gateway + " / " + link.netmask;
        lv_label_set_text(detail_gateway_label_, net.c_str());
        lv_label_set_text_fmt(detail_rssi_label_, "%s（%d dBm）", SignalWord(link.rssi),
                              static_cast<int>(link.rssi));
    } else {
        lv_label_set_text(detail_gateway_label_, "—");
        lv_label_set_text(detail_rssi_label_, connected ? "未知" : "—");
    }
    if (wifi_detail_connect_button_ != nullptr) {
        if (saved.empty()) lv_obj_add_state(wifi_detail_connect_button_, LV_STATE_DISABLED);
        else lv_obj_clear_state(wifi_detail_connect_button_, LV_STATE_DISABLED);
    }
}

void SettingsApp::ConnectToNetwork(const std::string& ssid, const std::string& password) {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr) {
        lv_label_set_text(wifi_status_label_, "Wi-Fi 不可用");
        return;
    }

    connecting_ssid_ = ssid;
    const std::string connecting = "正在连接 " + ssid + "…";
    lv_label_set_text(wifi_status_label_, connecting.c_str());

    auto guard = wifi_async_guard_;
    const uint32_t generation = guard != nullptr ? guard->generation.load() : 0;
    const bool started = wifi->Connect(ssid, password, [guard, generation, ssid, password](WiFiStatus status) {
        auto* payload = new WiFiConnectPayload{
            .guard = guard,
            .generation = generation,
            .status = status,
            .ssid = ssid,
            .password = password,
        };
        QueueWiFiAsyncPayload(payload, WiFiConnectCompleteCallback);
    });
    if (!started) {
        const std::string failed = "无法连接 " + ssid + "，请稍后重试";
        lv_label_set_text(wifi_status_label_, failed.c_str());
    }
}

void SettingsApp::OnWiFiConnectAsyncComplete(WiFiStatus status,
                                             const std::string& ssid,
                                             const std::string& password) {
    if (status == WiFiStatus::kConnected) {
        if (!wifi_config_.SaveCredentials(ssid, password)) {
            ui_->ShowToastUnlocked("已连接，但密码没有保存");
        }
    }
    OnConnectResult(status, ssid);
}

void SettingsApp::OnConnectResult(WiFiStatus status, const std::string& ssid) {
    auto* wifi = context_->services().wifi();
    switch (status) {
        case WiFiStatus::kConnected: {
            const std::string text = "已连接 " + ssid;
            lv_label_set_text(wifi_status_label_, text.c_str());
            ESP_LOGI(TAG, "WiFi connected: %s, IP: %s", ssid.c_str(),
                     wifi != nullptr ? wifi->GetIPAddress().c_str() : "unknown");
            // Re-render so the connected network moves into the top group.
            OnWiFiScanComplete(wifi_scan_results_);
            break;
        }
        case WiFiStatus::kFailed:
            ESP_LOGE(TAG, "WiFi connection failed: %s", ssid.c_str());
            if (wifi != nullptr && wifi->LastFailureWasAuthRejected()) {
                // A changed router password lands here; ask for the new one straight away.
                lv_label_set_text(wifi_status_label_, "密码错误");
                ShowPasswordDialog(ssid, "密码错误，请重新输入");
            } else {
                const std::string text = "无法连接 " + ssid;
                lv_label_set_text(wifi_status_label_, text.c_str());
            }
            break;
        case WiFiStatus::kDisconnected:
            lv_label_set_text(wifi_status_label_, "未连接");
            break;
        case WiFiStatus::kConnecting: {
            const std::string text = "正在连接 " + ssid + "…";
            lv_label_set_text(wifi_status_label_, text.c_str());
            break;
        }
    }
}
