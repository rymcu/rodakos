#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_os/time_service.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

#include <esp_log.h>
#include <string>

namespace {
constexpr const char* TAG = "SettingsApp";
}  // namespace

using namespace rodakos_settings;
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
