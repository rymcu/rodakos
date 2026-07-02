#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/device_cloud_config.h"
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
struct CloudRefreshPayload {
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
