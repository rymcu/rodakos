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

using namespace rodakos_settings;

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

lv_obj_t* CreateWiFiGroup(lv_obj_t* parent, lv_coord_t y, int rows) {
    return CreateGroupList(parent, kGridX, y, kGridWidth, kRowHeight * rows);
}

// One network row: name on the left, "🔒 中" or status on the right.
lv_obj_t* CreateNetworkRow(lv_obj_t* list, int index, const std::string& ssid,
                           const std::string& value, lv_color_t value_color) {
    auto* row = CreateListRow(list, index);
    CreateSettingIcon(row, ICON_WIFI);

    auto* name = CreateSettingLabel(row, ssid.c_str(), false);
    lv_obj_set_width(name, 150);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 28, 0);

    auto* status = CreateRowValue(row, 90, false);
    lv_label_set_text(status, value.c_str());
    lv_obj_set_style_text_color(status, value_color, 0);
    return row;
}

lv_obj_t* CreateWideButton(lv_obj_t* parent, const char* text, SettingButtonStyle style,
                           lv_coord_t x, lv_coord_t width) {
    auto* button = CreateSettingButton(parent, text, style, width, kActionHeight);
    lv_obj_set_pos(button, x, kActionY);
    return button;
}

lv_obj_t* CreateDetailRow(lv_obj_t* list, int index, const char* name) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, kGridWidth, 26);
    lv_obj_set_pos(row, 0, 26 * index + 4);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    auto* key = CreateSettingLabel(row, name, true);
    lv_obj_set_style_text_font(key, &phone_font_12, 0);
    lv_obj_align(key, LV_ALIGN_LEFT_MID, 0, 0);
    auto* value = CreateSettingLabel(row, "", false);
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

    lv_obj_t* scan_label = nullptr;
    auto* scan_btn = CreateSettingButton(wifi_body_, "重新扫描", SettingButtonStyle::kSecondary, 84, 30,
                                         &scan_label);
    lv_obj_set_style_text_font(scan_label, &phone_font_12, 0);
    lv_obj_set_pos(scan_btn, kGridX + kGridWidth - 84, 2);
    lv_obj_set_ext_click_area(scan_btn, 6);
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
        auto* list = CreateWiFiGroup(wifi_list_container_, y, 1);
        const bool is_current = !current_ssid.empty();
        auto* row = CreateNetworkRow(list, 0, pinned_ssid, is_current ? "已连接  " ICON_INFO : "已保存  " ICON_INFO,
                                     is_current ? rodakos_theme_success() : rodakos_theme_text_secondary());
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
        CreateGroupCaption(wifi_list_container_, "其他网络", y);
        auto* list = CreateWiFiGroup(wifi_list_container_, y, static_cast<int>(others.size()));
        for (size_t row_index = 0; row_index < others.size(); ++row_index) {
            const auto& ap = results[others[row_index]];
            const std::string value = std::string(ap.is_secured ? ICON_LOCK " " : "开放 · ") + SignalWord(ap.rssi);
            auto* row = CreateNetworkRow(list, static_cast<int>(row_index), ap.ssid, value,
                                         rodakos_theme_text_secondary());
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
    lv_obj_set_style_radius(box, kListRadius, 0);
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
    lv_obj_set_style_radius(password_textarea_, kButtonRadius, 0);

    // Long passwords are easy to mistype on a small keyboard; let the user check them.
    lv_obj_t* reveal_label = nullptr;
    auto* reveal = CreateSettingButton(box, "显示", SettingButtonStyle::kSecondary, 48,
                                       kDialogButtonHeight, &reveal_label);
    lv_obj_set_style_text_font(reveal_label, &phone_font_12, 0);
    lv_obj_align(reveal, LV_ALIGN_BOTTOM_LEFT, 164, 0);
    lv_obj_add_event_cb(reveal, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* button = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        if (self->password_textarea_ == nullptr) return;
        const bool hidden = lv_textarea_get_password_mode(self->password_textarea_);
        lv_textarea_set_password_mode(self->password_textarea_, !hidden);
        lv_label_set_text(lv_obj_get_child(button, 0), hidden ? "隐藏" : "显示");
    }, LV_EVENT_CLICKED, this);

    // 取消 / 连接 share the field row; the keyboard's OK key also connects.
    lv_obj_t* cancel_label = nullptr;
    auto* cancel = CreateSettingButton(box, ICON_CLOSE, SettingButtonStyle::kSecondary, 36,
                                       kDialogButtonHeight, &cancel_label);
    lv_obj_set_style_text_font(cancel_label, PhoneIconFont(), 0);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ClosePasswordDialog();
    }, LV_EVENT_CLICKED, this);

    auto* connect = CreateSettingButton(box, "连接", SettingButtonStyle::kPrimary, 64, kDialogButtonHeight);
    lv_obj_align(connect, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
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

    auto* card = CreateGroupList(wifi_detail_body_, kGridX, 6, kGridWidth, 136);

    detail_ssid_label_ = CreateDetailRow(card, 0, "网络");
    detail_status_label_ = CreateDetailRow(card, 1, "状态");
    detail_ip_label_ = CreateDetailRow(card, 2, "IP 地址");
    detail_gateway_label_ = CreateDetailRow(card, 3, "网关 / 掩码");
    detail_rssi_label_ = CreateDetailRow(card, 4, "信号");
    detail_netmask_label_ = nullptr;

    wifi_detail_connect_button_ = CreateWideButton(wifi_detail_body_, "重新输入密码", SettingButtonStyle::kSecondary,
                                                   kGridX, (kGridWidth - 8) / 2);
    lv_obj_add_event_cb(wifi_detail_connect_button_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        const std::string ssid = self->wifi_config_.GetSavedSSID();
        if (!ssid.empty()) self->ShowPasswordDialog(ssid);
    }, LV_EVENT_CLICKED, this);

    auto* forget = CreateWideButton(wifi_detail_body_, "忘记此网络", SettingButtonStyle::kDangerOutline,
                                    kGridX + (kGridWidth - 8) / 2 + 8, (kGridWidth - 8) / 2);
    lv_obj_add_event_cb(forget, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowForgetWiFiDialog();
    }, LV_EVENT_CLICKED, this);

    UpdateWiFiDetailPage();
}

void SettingsApp::ShowForgetWiFiDialog() {
    const std::string ssid = wifi_config_.GetSavedSSID();
    if (ssid.empty() || wifi_forget_dialog_ != nullptr) return;

    const SettingDialog dialog = CreateSettingDialog(150);
    wifi_forget_dialog_ = dialog.layer;
    const std::string title_text = "忘记「" + ssid + "」？";
    CreateDialogTitle(dialog.box, title_text.c_str());
    CreateDialogMessage(dialog.box, "设备会断开这个网络并删除保存的密码，下次需要重新输入。", 30);
    CreateDialogButtons(dialog.box, "忘记", SettingButtonStyle::kDanger,
        [](lv_event_t* e) {
            static_cast<SettingsApp*>(lv_event_get_user_data(e))->CloseForgetWiFiDialog();
        },
        [](lv_event_t* e) {
            auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
            self->CloseForgetWiFiDialog();
            if (!self->wifi_config_.ClearCredentials()) {
                self->ui_->ShowToastUnlocked("删除失败，请重试");
                return;
            }
            if (auto* wifi = self->context_->services().wifi(); wifi != nullptr) wifi->Disconnect();
            self->ui_->ShowToastUnlocked("已忘记该网络");
            self->ShowPage(SettingsPage::kWiFiList);
        },
        this);
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

    // The rejection belongs to the network that was tried, which may differ from the saved one.
    const bool rejected = !connected && !ssid.empty() && wifi->AuthRejectedSSID() == ssid;
    const char* status_text = connected ? "已连接"
        : status == WiFiStatus::kConnecting ? "正在连接…"
        : rejected ? "密码错误"
        : "未连接";
    lv_label_set_text(detail_status_label_, status_text);
    lv_obj_set_style_text_color(detail_status_label_,
                                connected ? rodakos_theme_success()
                                : rejected ? rodakos_theme_error()
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
        case WiFiStatus::kFailed: {
            ESP_LOGE(TAG, "WiFi connection failed: %s", ssid.c_str());
            const bool rejected = wifi != nullptr && wifi->AuthRejectedSSID() == ssid;
            // Switching to another network dropped the saved one. Its credentials are still
            // stored (only a successful connection replaces them), so go back to it.
            std::string saved_ssid;
            std::string saved_password;
            const bool fall_back = wifi_config_.LoadCredentials(saved_ssid, saved_password) &&
                                   saved_ssid != ssid;
            if (fall_back) {
                ESP_LOGI(TAG, "Reconnecting to saved network %s", saved_ssid.c_str());
                ConnectToNetwork(saved_ssid, saved_password);
            }
            if (rejected) {
                // A wrong or changed password lands here; ask for it again straight away.
                lv_label_set_text(wifi_status_label_, "密码错误");
                ShowPasswordDialog(ssid, "密码错误，请重新输入");
            } else if (!fall_back) {
                const std::string text = "无法连接 " + ssid;
                lv_label_set_text(wifi_status_label_, text.c_str());
            }
            break;
        }
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
