#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/device_cloud_config.h"
#include "phone_os/device_pairing_policy.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <string>

namespace {
bool IsExpectedPairingWait(const std::string& error) {
    return error.empty() || error.rfind("等待设备绑定确认", 0) == 0;
}

struct CloudRefreshPayload {
    std::shared_ptr<SettingsCloudRefreshGuard> guard;
    rodakos::DeviceCloudConfigService* service = nullptr;
    uint32_t generation = 0;
    bool ok = false;
    rodakos::DeviceCloudConfig config;
    std::string error;
};

struct CloudUnbindPayload {
    std::shared_ptr<SettingsCloudRefreshGuard> guard;
    rodakos::DeviceCloudConfigService* service = nullptr;
    uint32_t generation = 0;
    bool ok = false;
    rodakos::DeviceCloudConfig config;
    std::string error;
};

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

void CloudUnbindCompleteCallback(void* user_data) {
    auto* payload = static_cast<CloudUnbindPayload*>(user_data);
    auto guard = payload != nullptr ? payload->guard : nullptr;
    SettingsApp* app = guard ? guard->app.load() : nullptr;
    if (app != nullptr) {
        app->OnDeviceCloudUnbindComplete(payload->ok, payload->error,
                                         payload->generation);
    }
    delete payload;
}

void CloudUnbindTask(void* arg) {
    auto* payload = static_cast<CloudUnbindPayload*>(arg);
    if (payload != nullptr && payload->service != nullptr) {
        payload->ok = payload->service->Unbind(payload->config);
        payload->error = payload->ok ? std::string() : payload->service->last_error();
        bool queued = false;
        if (lvgl_port_lock(1000)) {
            queued = lv_async_call(CloudUnbindCompleteCallback, payload) == LV_RESULT_OK;
            lvgl_port_unlock();
        }
        if (!queued) {
            if (payload->guard) {
                payload->guard->refresh_in_progress.store(false);
            }
            delete payload;
        }
    } else {
        delete payload;
    }
    vTaskDelete(nullptr);
}
}  // namespace

using namespace rodakos_settings;
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

    auto* guide_card = CreateSettingCard(device_cloud_body_, 4, 44);
    lv_obj_set_style_pad_all(guide_card, 8, 0);
    cloud_guide_label_ = CreateSettingLabel(
        guide_card, "连接 Rodak 后，可在电脑上管理和控制设备。", true);
    lv_obj_set_width(cloud_guide_label_, 272);
    lv_label_set_long_mode(cloud_guide_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(cloud_guide_label_, LV_ALIGN_LEFT_MID, 0, 0);

    cloud_pairing_button_ = lv_btn_create(device_cloud_body_);
    lv_obj_set_size(cloud_pairing_button_, 288, 38);
    lv_obj_set_pos(cloud_pairing_button_, 4, 258);
    lv_obj_set_style_bg_color(cloud_pairing_button_, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(cloud_pairing_button_, 7, 0);
    cloud_pairing_button_label_ = lv_label_create(cloud_pairing_button_);
    lv_label_set_text(cloud_pairing_button_label_, "开始连接");
    lv_obj_set_style_text_color(cloud_pairing_button_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(cloud_pairing_button_label_, &phone_font_14, 0);
    lv_obj_center(cloud_pairing_button_label_);
    lv_obj_add_event_cb(cloud_pairing_button_, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->RefreshDeviceCloud();
    }, LV_EVENT_CLICKED, this);

    auto* status_card = CreateSettingCard(device_cloud_body_, 56, 52);
    lv_obj_set_style_pad_all(status_card, 10, 0);

    auto* status_icon = lv_label_create(status_card);
    lv_label_set_text(status_icon, FONT_AWESOME_CLOUD);
    lv_obj_set_style_text_color(status_icon, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(status_icon, PhoneIconFont(), 0);
    lv_obj_align(status_icon, LV_ALIGN_LEFT_MID, 0, 0);

    auto* status_title = CreateSettingLabel(status_card, "连接状态", true);
    lv_obj_set_style_text_font(status_title, &phone_font_12, 0);
    lv_obj_align(status_title, LV_ALIGN_TOP_LEFT, 32, 0);

    cloud_status_label_ = CreateSettingLabel(status_card, "Idle", false);
    lv_obj_set_width(cloud_status_label_, 236);
    lv_label_set_long_mode(cloud_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(cloud_status_label_, LV_ALIGN_BOTTOM_LEFT, 32, 0);

    auto* url_card = CreateSettingCard(device_cloud_body_, 116, 62);
    lv_obj_set_style_pad_all(url_card, 8, 0);

    auto* url_icon = lv_label_create(url_card);
    lv_label_set_text(url_icon, FONT_AWESOME_LINK);
    lv_obj_set_style_text_color(url_icon, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(url_icon, PhoneIconFont(), 0);
    lv_obj_align(url_icon, LV_ALIGN_LEFT_MID, 0, 0);

    auto* url_title = CreateSettingLabel(url_card, "服务地址", true);
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

    auto* id_card = CreateSettingCard(device_cloud_body_, 304, 40);
    lv_obj_set_style_pad_all(id_card, 8, 0);
    cloud_client_id_label_ = CreateSettingLabel(id_card, "设备 ID", true);
    lv_obj_set_width(cloud_client_id_label_, 264);
    lv_label_set_long_mode(cloud_client_id_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(cloud_client_id_label_, LV_ALIGN_LEFT_MID, 0, 0);

    auto* voice_card = CreateSettingCard(device_cloud_body_, 352, 40);
    lv_obj_set_style_pad_all(voice_card, 8, 0);
    cloud_realtime_voice_label_ = CreateSettingLabel(voice_card, "实时语音流：未配置", true);
    lv_obj_set_width(cloud_realtime_voice_label_, 264);
    lv_label_set_long_mode(cloud_realtime_voice_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(cloud_realtime_voice_label_, LV_ALIGN_LEFT_MID, 0, 0);

    auto* activation_card = CreateSettingCard(device_cloud_body_, 186, 64);
    lv_obj_set_style_pad_all(activation_card, 8, 0);
    cloud_activation_label_ = CreateSettingLabel(activation_card, "尚未连接", true);
    lv_obj_set_width(cloud_activation_label_, 272);
    lv_label_set_long_mode(cloud_activation_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(cloud_activation_label_, &phone_font_18, 0);
    lv_obj_align(cloud_activation_label_, LV_ALIGN_LEFT_MID, 0, 0);

    cloud_unbind_button_ = lv_btn_create(device_cloud_body_);
    lv_obj_set_size(cloud_unbind_button_, 288, 38);
    lv_obj_set_pos(cloud_unbind_button_, 4, 258);
    lv_obj_set_style_bg_color(cloud_unbind_button_, lv_palette_main(LV_PALETTE_RED), 0);
    auto* unbind_label = lv_label_create(cloud_unbind_button_);
    lv_label_set_text(unbind_label, "解除与 Rodak 的连接");
    lv_obj_set_style_text_font(unbind_label, &phone_font_12, 0);
    lv_obj_center(unbind_label);
    lv_obj_add_event_cb(cloud_unbind_button_, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowDeviceCloudUnbindDialog();
    }, LV_EVENT_CLICKED, this);

    cloud_pairing_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
        if (self != nullptr && self->cloud_refresh_guard_ &&
            !self->cloud_refresh_guard_->refresh_in_progress.load()) {
            self->RefreshDeviceCloud();
        }
    }, 2000, this);
    lv_timer_pause(cloud_pairing_timer_);
    UpdateDeviceCloudPage();
}

void SettingsApp::UpdateDeviceCloudPage() {
    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (device_cloud == nullptr || cloud_status_label_ == nullptr) {
        return;
    }

    rodakos::DeviceCloudConfig config;
    device_cloud->Load(config);
    if (!config.pairing_code.empty()) {
        cloud_pairing_code_ = config.pairing_code;
    }
    const std::string pairing_code = !config.pairing_code.empty()
                                         ? config.pairing_code
                                         : cloud_pairing_code_;
    const bool aiot_bound = config.has_aiot_config;
    const bool pairing_pending = config.has_pairing_request &&
        rodakos::ClassifyDevicePairingStatus(config.pairing_status) ==
            rodakos::DevicePairingStatus::kPending;
    const bool pairing_error = config.has_pairing_request && !pairing_pending;
    lv_label_set_text(cloud_status_label_,
                      aiot_bound
                          ? "已绑定到 Rodak"
                          : (!cloud_pairing_error_.empty()
                                 ? cloud_pairing_error_.c_str()
                                 : (pairing_pending
                                        ? "等待在 Rodak 中确认"
                                        : (pairing_error ? "配对状态异常，请重试"
                                                         : "尚未绑定到 Rodak"))));
    lv_label_set_text(cloud_url_label_, config.provisioning_url.c_str());
    const std::string client_id = "设备 ID: " + device_cloud->GetClientId();
    lv_label_set_text(cloud_client_id_label_, client_id.c_str());
    lv_label_set_text_fmt(
        cloud_realtime_voice_label_, "实时语音流：%s",
         config.has_realtime_voice_config ? "Rodak realtime voice"
                                          : "未配置");
    if (!aiot_bound && !pairing_code.empty() &&
        (config.has_pairing_request || !cloud_pairing_error_.empty())) {
        lv_label_set_text_fmt(cloud_activation_label_, "配对码  %s",
                              pairing_code.c_str());
    } else if (config.has_activation_code) {
        lv_label_set_text_fmt(cloud_activation_label_, "激活码：%s",
                              config.activation_code.c_str());
    } else {
        lv_label_set_text(cloud_activation_label_,
                          aiot_bound ? "绑定成功" : "尚未绑定");
    }
    if (cloud_guide_label_ != nullptr) {
        lv_label_set_text(cloud_guide_label_,
                          aiot_bound
                              ? "设备云服务已启用，可接收控制和更新。"
                              : (!cloud_pairing_error_.empty() || pairing_error
                                     ? "配对码仍然有效，请检查网络后点击重试。"
                                     : (pairing_pending
                                            ? "在 Rodak 中输入配对码，本页会自动检查结果。"
                                             : (config.has_realtime_voice_config
                                                    ? "实时语音流已配置；仍可绑定 Rodak 设备云。"
                                                    : "1. 点击下方按钮\n2. 在 Rodak 中输入配对码"))));
    }
    if (cloud_pairing_button_label_ != nullptr) {
        lv_label_set_text(cloud_pairing_button_label_,
                          aiot_bound
                              ? "已绑定"
                              : ((!cloud_pairing_error_.empty() || pairing_error)
                                     ? "重试"
                                     : (pairing_pending ? "检查连接" : "开始连接")));
    }
    if (cloud_pairing_button_ != nullptr) {
        lv_obj_clear_state(cloud_pairing_button_, LV_STATE_DISABLED);
        if (aiot_bound) {
            lv_obj_add_flag(cloud_pairing_button_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(cloud_pairing_button_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (cloud_unbind_button_ != nullptr) {
        if (aiot_bound) {
            lv_obj_clear_flag(cloud_unbind_button_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(cloud_unbind_button_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (cloud_pairing_timer_ != nullptr) {
        if (current_page_ == SettingsPage::kDeviceCloud &&
            pairing_pending && !aiot_bound && cloud_pairing_error_.empty()) {
            lv_timer_resume(cloud_pairing_timer_);
            lv_timer_reset(cloud_pairing_timer_);
        } else {
            lv_timer_pause(cloud_pairing_timer_);
        }
    }
}

void SettingsApp::ShowDeviceCloudUnbindDialog() {
    if (cloud_unbind_dialog_ != nullptr) {
        return;
    }

    cloud_unbind_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(cloud_unbind_dialog_);
    lv_obj_set_size(cloud_unbind_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cloud_unbind_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(cloud_unbind_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(cloud_unbind_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* box = lv_obj_create(cloud_unbind_dialog_);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 288, 154);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 14, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(box, "解除与 Rodak 的连接？");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    auto* message = CreateSettingLabel(
        box, "解除后将无法在 Rodak 中控制此设备，需要重新配对才能恢复。", true);
    lv_obj_set_width(message, 252);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 34);

    auto* cancel = lv_btn_create(box);
    lv_obj_set_size(cancel, 108, 34);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cancel, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel, 6, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    auto* cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "取消");
    lv_obj_set_style_text_color(cancel_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(cancel_label, &phone_font_14, 0);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->CloseDeviceCloudUnbindDialog();
    }, LV_EVENT_CLICKED, this);

    auto* confirm = lv_btn_create(box);
    lv_obj_set_size(confirm, 108, 34);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(confirm, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_radius(confirm, 6, 0);
    lv_obj_set_style_shadow_width(confirm, 0, 0);
    auto* confirm_label = lv_label_create(confirm);
    lv_label_set_text(confirm_label, "确认解除");
    lv_obj_set_style_text_color(confirm_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(confirm_label, &phone_font_14, 0);
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->CloseDeviceCloudUnbindDialog();
        self->UnbindDeviceCloud();
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::CloseDeviceCloudUnbindDialog() {
    if (cloud_unbind_dialog_ != nullptr && lv_obj_is_valid(cloud_unbind_dialog_)) {
        lv_obj_delete(cloud_unbind_dialog_);
    }
    cloud_unbind_dialog_ = nullptr;
}

void SettingsApp::UnbindDeviceCloud() {
    auto* wifi = context_ != nullptr ? context_->services().wifi() : nullptr;
    auto* service = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (wifi == nullptr || wifi->GetStatus() != WiFiStatus::kConnected) {
        ui_->ShowToastUnlocked("需联网后才能解除绑定");
        return;
    }
    bool expected = false;
    if (service == nullptr || !cloud_refresh_guard_ ||
        !cloud_refresh_guard_->refresh_in_progress.compare_exchange_strong(expected, true)) {
        ui_->ShowToastUnlocked("设备服务忙碌");
        return;
    }
    auto* payload = new CloudUnbindPayload;
    payload->guard = cloud_refresh_guard_;
    payload->service = service;
    payload->generation = cloud_refresh_guard_->refresh_generation.load();
    if (xTaskCreate(CloudUnbindTask, "cloud_unbind", 6144, payload, 3, nullptr) != pdPASS) {
        cloud_refresh_guard_->refresh_in_progress.store(false);
        delete payload;
        ui_->ShowToastUnlocked("无法启动解绑任务");
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
        lv_label_set_text(cloud_status_label_, "正在连接 Rodak...");
    }

    cloud_pairing_error_.clear();
    if (cloud_pairing_button_ != nullptr) {
        lv_obj_add_state(cloud_pairing_button_, LV_STATE_DISABLED);
    }
    if (cloud_pairing_button_label_ != nullptr) {
        lv_label_set_text(cloud_pairing_button_label_, "请稍候...");
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
    const bool pairing_pending = config.has_pairing_request &&
        rodakos::ClassifyDevicePairingStatus(config.pairing_status) ==
            rodakos::DevicePairingStatus::kPending;
    cloud_pairing_error_ = (!ok && (!pairing_pending || !IsExpectedPairingWait(error)))
                               ? (error.empty() ? "连接 Rodak 失败，请重试" : error)
                               : std::string();
    UpdateDeviceCloudPage();
    if (ok && config.has_aiot_config) {
        if (context_ != nullptr) {
            context_->services().NotifyDeviceCloudBound();
        }
        ui_->ShowToastUnlocked("设备绑定成功");
        if (cloud_status_label_ != nullptr) {
            lv_label_set_text(cloud_status_label_,
                              "已绑定到 Rodak");
        }
    } else if (!ok && pairing_pending && IsExpectedPairingWait(error)) {
        if (cloud_status_label_ != nullptr) {
            lv_label_set_text(cloud_status_label_, "等待在 Rodak 中确认");
        }
        if (cloud_activation_label_ != nullptr && !config.pairing_code.empty()) {
            lv_label_set_text_fmt(cloud_activation_label_, "配对码  %s",
                                  config.pairing_code.c_str());
        }
    } else if (!ok) {
        ui_->ShowToastUnlocked(error.empty() ? "Device services failed" : error.c_str());
        if (cloud_status_label_ != nullptr) {
            lv_label_set_text(cloud_status_label_, error.empty() ? "Refresh failed" : error.c_str());
        }
        if (config.has_activation_code && cloud_activation_label_ != nullptr) {
            lv_label_set_text_fmt(cloud_activation_label_, "Activation: %s",
                                  config.activation_code.c_str());
        }
    }
}

void SettingsApp::OnDeviceCloudUnbindComplete(bool ok,
                                              const std::string& error,
                                              uint32_t generation) {
    if (ui_ == nullptr || !cloud_refresh_guard_ ||
        generation != cloud_refresh_guard_->refresh_generation.load()) {
        return;
    }
    cloud_refresh_guard_->refresh_in_progress.store(false);
    cloud_pairing_error_.clear();
    if (ok) {
        cloud_pairing_code_.clear();
    }
    UpdateDeviceCloudPage();
    if (ok && context_ != nullptr) {
        context_->services().NotifyDeviceCloudUnbound();
    }
    ui_->ShowToastUnlocked(ok ? "设备已解除绑定"
                              : (error.empty() ? "设备解绑失败" : error.c_str()));
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

    auto* title = CreateSettingLabel(dialog_box, "服务地址");
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
    lv_label_set_text(cancel_label, "取消");
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
    lv_label_set_text(save_label, "保存");
    lv_obj_set_style_text_color(save_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(save_label, &phone_font_12, 0);
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        const bool saved = self->SaveCloudProvisioningUrl(
            TrimCloudUrl(lv_textarea_get_text(self->cloud_url_textarea_)));
        if (saved) {
            self->CloseCloudProvisioningUrlDialog();
        }
    }, LV_EVENT_CLICKED, this);

    soft_keyboard_.Show(cloud_url_textarea_);
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

bool SettingsApp::SaveCloudProvisioningUrl(const std::string& url) {
    auto* device_cloud = context_ != nullptr ? context_->services().device_cloud() : nullptr;
    if (device_cloud == nullptr) {
        return false;
    }
    const auto result = device_cloud->SaveProvisioningUrl(url);
    if (result != rodakos::ProvisioningUrlSaveResult::kSaved &&
        result != rodakos::ProvisioningUrlSaveResult::kUnchanged) {
        ui_->ShowToastUnlocked("Failed to save provisioning endpoint");
        return false;
    }
    ui_->ShowToastUnlocked(result == rodakos::ProvisioningUrlSaveResult::kUnchanged
                               ? "服务地址未变更"
                               : "服务地址已保存");
    UpdateDeviceCloudPage();
    return true;
}
