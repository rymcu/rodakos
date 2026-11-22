#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_os/time_service.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

#include <esp_log.h>

#include <cstdio>
#include <ctime>
#include <string>

// Date & Time: current time and last sync at the top, time zone and server as 38 px rows that
// open full-screen pickers, and one 40 px 立即校时 button at the bottom of the 320x200 body.
namespace {
constexpr const char* TAG = "SettingsApp";
using namespace rodakos_settings;

const char* kWeekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

lv_obj_t* CreateValueRow(lv_obj_t* list, int index, const char* icon, const char* title,
                         lv_obj_t** value_out) {
    auto* row = CreateListRow(list, index);
    CreateSettingIcon(row, icon);
    auto* title_label = CreateSettingLabel(row, title, false);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 28, 0);
    *value_out = CreateRowValue(row, 140, true);
    CreateRowChevron(row);
    return row;
}

std::string ServerLabel(const std::string& server) {
    size_t count = 0;
    const RodakNtpServer* servers = TimeServiceNtpServers(&count);
    for (size_t index = 0; index < count; ++index) {
        if (server == servers[index].server && servers[index].server[0] != '\0') return server;
    }
    return server.empty() ? "未设置" : server;
}
}  // namespace

using namespace rodakos_settings;

void SettingsApp::CreateDateTimePage() {
    datetime_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(datetime_body_);
    lv_obj_set_size(datetime_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(datetime_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(datetime_body_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(datetime_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(datetime_body_, LV_OBJ_FLAG_HIDDEN);

    datetime_clock_label_ = lv_label_create(datetime_body_);
    lv_obj_set_style_text_font(datetime_clock_label_, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(datetime_clock_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_pos(datetime_clock_label_, kGridX + 4, 0);

    datetime_date_label_ = CreateSettingLabel(datetime_body_, "", true);
    lv_obj_set_style_text_font(datetime_date_label_, &phone_font_12, 0);
    lv_obj_set_pos(datetime_date_label_, kGridX + 96, 10);

    time_sync_status_label_ = CreateSettingLabel(datetime_body_, "", true);
    lv_obj_set_width(time_sync_status_label_, kGridWidth - 8);
    lv_label_set_long_mode(time_sync_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(time_sync_status_label_, &phone_font_12, 0);
    lv_obj_set_pos(time_sync_status_label_, kGridX + 4, 34);

    auto* list = CreateGroupList(datetime_body_, kGridX, 58, kGridWidth, kRowHeight * 2);

    auto* tz_row = CreateValueRow(list, 0, ICON_GLOBE, "时区", &timezone_dropdown_);
    lv_obj_add_event_cb(tz_row, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowDateTimePicker(false);
    }, LV_EVENT_CLICKED, this);
    auto* server_row = CreateValueRow(list, 1, ICON_CLOCK, "校时服务器", &ntp_dropdown_);
    lv_obj_add_event_cb(server_row, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->ShowDateTimePicker(true);
    }, LV_EVENT_CLICKED, this);

    time_sync_button_ = CreateSettingButton(datetime_body_, "立即校时", SettingButtonStyle::kPrimary,
                                            kGridWidth, kActionHeight);
    lv_obj_set_pos(time_sync_button_, kGridX, kActionY);
    lv_obj_add_event_cb(time_sync_button_, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->StartTimeSync();
    }, LV_EVENT_CLICKED, this);

    // The clock and the last-sync line refresh once per second while the page exists.
    datetime_clock_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
        if (self != nullptr) self->UpdateDateTimePage();
    }, 1000, this);
    UpdateDateTimePage();
}

void SettingsApp::UpdateDateTimePage() {
    if (datetime_clock_label_ == nullptr) return;
    if (TimeServiceTimeIsValid()) {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        lv_label_set_text_fmt(datetime_clock_label_, "%02d:%02d", local.tm_hour, local.tm_min);
        lv_label_set_text_fmt(datetime_date_label_, "%d 月 %d 日 %s", local.tm_mon + 1, local.tm_mday,
                              kWeekdays[local.tm_wday % 7]);
    } else {
        lv_label_set_text(datetime_clock_label_, "--:--");
        lv_label_set_text(datetime_date_label_, "时间尚未校准");
    }

    if (!time_sync_in_progress_) {
        const int64_t last = TimeServiceLastSyncEpoch();
        if (last > 0) {
            const std::time_t at = static_cast<std::time_t>(last);
            std::tm local{};
            localtime_r(&at, &local);
            lv_label_set_text_fmt(time_sync_status_label_, "上次校时：%d 月 %d 日 %02d:%02d",
                                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
            lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_success(), 0);
        } else {
            lv_label_set_text(time_sync_status_label_,
                              TimeServiceTimeIsValid() ? "本次开机尚未校时" : "联网后会自动校时");
            lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_text_secondary(), 0);
        }
    }

    lv_label_set_text(timezone_dropdown_, home_time_zone_label_.empty()
                                              ? TimeServiceLoadTimeZone().c_str()
                                              : home_time_zone_label_.c_str());
    lv_label_set_text(ntp_dropdown_, ServerLabel(datetime_server_cache_).c_str());
}

void SettingsApp::SaveTimeZone(size_t index) {
    size_t count = 0;
    const auto* zones = TimeServiceTimeZones(&count);
    if (index >= count) {
        return;
    }
    if (!TimeServiceSaveTimeZone(zones[index].tz)) {
        ui_->ShowToastUnlocked("时区保存失败，请重试");
        return;
    }
    home_time_zone_label_ = zones[index].label;
    TimeServiceApplyTimeZone(zones[index].tz);
    ESP_LOGI(TAG, "Time zone changed: %s (%s)", zones[index].label, zones[index].tz);
    UpdateDateTimePage();
}

bool SettingsApp::SaveNtpServer(const std::string& server) {
    if (server.empty()) {
        return false;
    }
    if (!TimeServiceSaveNtpServer(server)) {
        ui_->ShowToastUnlocked("服务器保存失败，请重试");
        return false;
    }
    datetime_server_cache_ = server;
    ESP_LOGI(TAG, "NTP server changed: %s", server.c_str());
    UpdateDateTimePage();
    return true;
}

void SettingsApp::ShowDateTimePicker(bool servers) {
    CloseDateTimePicker();
    datetime_picker_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(datetime_picker_);
    lv_obj_set_size(datetime_picker_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(datetime_picker_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(datetime_picker_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(datetime_picker_, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(datetime_picker_, servers ? "选择校时服务器" : "选择时区", false);
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_set_pos(title, kGridX + 4, 8);

    auto* list = CreateGroupList(datetime_picker_, kGridX, 38, kGridWidth, 150);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    size_t count = 0;
    const std::string current = servers ? datetime_server_cache_ : TimeServiceLoadTimeZone();
    const RodakNtpServer* server_list = servers ? TimeServiceNtpServers(&count) : nullptr;
    const RodakTimeZone* zone_list = servers ? nullptr : TimeServiceTimeZones(&count);
    bool current_listed = false;
    for (size_t index = 0; index < count; ++index) {
        const bool custom = servers && server_list[index].server[0] == '\0';
        const char* label = servers ? (custom ? "自定义…" : server_list[index].server)
                                    : zone_list[index].label;
        const char* value = servers ? server_list[index].server : zone_list[index].tz;
        const bool selected = !custom && current == value;
        current_listed = current_listed || selected;

        auto* row = CreateListRow(list, static_cast<int>(index));
        auto* name = CreateSettingLabel(row, label, false);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        if (selected) {
            // Same tick as the 按键 action picker.
            auto* check = lv_label_create(row);
            lv_label_set_text(check, ICON_CHECK);
            lv_obj_set_style_text_color(check, rodakos_theme_primary(), 0);
            lv_obj_set_style_text_font(check, PhoneIconFont(), 0);
            lv_obj_align(check, LV_ALIGN_RIGHT_MID, 0, 0);
        } else if (custom && !current_listed && index + 1 == count && !current.empty()) {
            auto* value = CreateRowValue(row, 150, false);
            lv_label_set_text(value, current.c_str());
            lv_obj_set_style_text_color(value, rodakos_theme_primary(), 0);
        }
        lv_obj_set_user_data(row, reinterpret_cast<void*>(index | (servers ? 0x10000u : 0u)));
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
            const auto data = reinterpret_cast<uintptr_t>(
                lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
            const bool is_server = (data & 0x10000u) != 0;
            const size_t index = data & 0xffffu;
            // Picking deletes the picker that owns this row; run after the event returns.
            self->datetime_pending_choice_ = data;
            lv_async_call([](void* user_data) {
                auto* app = static_cast<SettingsApp*>(user_data);
                const uintptr_t choice = app->datetime_pending_choice_;
                app->CloseDateTimePicker();
                const bool server_choice = (choice & 0x10000u) != 0;
                const size_t picked = choice & 0xffffu;
                if (!server_choice) {
                    app->SaveTimeZone(picked);
                    return;
                }
                size_t total = 0;
                const RodakNtpServer* list = TimeServiceNtpServers(&total);
                if (picked >= total) return;
                if (list[picked].server[0] == '\0') app->ShowNtpServerDialog();
                else app->SaveNtpServer(list[picked].server);
            }, self);
            (void)is_server;
            (void)index;
        }, LV_EVENT_CLICKED, this);
    }

    auto* back = CreateSettingButton(datetime_picker_, "取消", SettingButtonStyle::kSecondary, kGridWidth,
                                     kActionHeight);
    lv_obj_set_pos(back, kGridX, 192);
    lv_obj_add_event_cb(back, [](lv_event_t* e) {
        lv_async_call([](void* user_data) {
            static_cast<SettingsApp*>(user_data)->CloseDateTimePicker();
        }, lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::CloseDateTimePicker() {
    if (datetime_picker_ != nullptr && lv_obj_is_valid(datetime_picker_)) {
        lv_obj_delete(datetime_picker_);
    }
    datetime_picker_ = nullptr;
}

void SettingsApp::StartTimeSync() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr || wifi->GetStatus() != WiFiStatus::kConnected) {
        if (time_sync_status_label_ != nullptr) {
            lv_label_set_text(time_sync_status_label_, "请先连接 Wi-Fi");
            lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_warning(), 0);
        }
        return;
    }
    if (time_sync_timer_ != nullptr) {
        lv_timer_delete(time_sync_timer_);
        time_sync_timer_ = nullptr;
    }
    if (!TimeServiceStartSavedSync()) {
        if (time_sync_status_label_ != nullptr) {
            lv_label_set_text(time_sync_status_label_, "校时没有开始，请检查服务器设置");
            lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_error(), 0);
        }
        return;
    }

    time_sync_in_progress_ = true;
    time_sync_poll_count_ = 0;
    time_sync_start_epoch_ = TimeServiceLastSyncEpoch();
    if (time_sync_button_ != nullptr) lv_obj_add_state(time_sync_button_, LV_STATE_DISABLED);
    if (time_sync_status_label_ != nullptr) {
        lv_label_set_text(time_sync_status_label_, "正在校时…");
        lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_primary(), 0);
    }
    time_sync_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<SettingsApp*>(lv_timer_get_user_data(timer));
        if (self != nullptr) {
            self->UpdateTimeSyncStatus();
        }
    }, 1000, this);
}

void SettingsApp::UpdateTimeSyncStatus() {
    if (!time_sync_in_progress_) {
        return;
    }
    time_sync_poll_count_++;
    // A newer callback timestamp is the success signal; it also catches a late success after the
    // wait below has given up, through the last-sync line in UpdateDateTimePage.
    const bool synced = TimeServiceGetSyncStatus() == TimeSyncStatus::kCompleted ||
                        TimeServiceLastSyncEpoch() != time_sync_start_epoch_;
    if (synced || time_sync_poll_count_ >= kTimeSyncTimeoutPolls) {
        time_sync_in_progress_ = false;
        if (time_sync_timer_ != nullptr) {
            lv_timer_delete(time_sync_timer_);
            time_sync_timer_ = nullptr;
        }
        if (time_sync_button_ != nullptr) lv_obj_clear_state(time_sync_button_, LV_STATE_DISABLED);
        if (!synced && time_sync_status_label_ != nullptr) {
            lv_label_set_text(time_sync_status_label_, "校时超时，请检查网络后重试");
            lv_obj_set_style_text_color(time_sync_status_label_, rodakos_theme_warning(), 0);
            return;
        }
        UpdateDateTimePage();
        return;
    }
    if (time_sync_status_label_ != nullptr) {
        lv_label_set_text_fmt(time_sync_status_label_, "正在校时… %lu 秒",
                              static_cast<unsigned long>(time_sync_poll_count_));
    }
}

void SettingsApp::ShowNtpServerDialog() {
    if (ntp_dialog_ != nullptr) {
        return;
    }

    ntp_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(ntp_dialog_);
    lv_obj_set_size(ntp_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ntp_dialog_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(ntp_dialog_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ntp_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    // Ends at y=116 so the 120 px keyboard never covers it.
    auto* dialog_box = lv_obj_create(ntp_dialog_);
    lv_obj_remove_style_all(dialog_box);
    lv_obj_set_size(dialog_box, 304, 108);
    lv_obj_set_pos(dialog_box, 8, 8);
    lv_obj_set_style_bg_color(dialog_box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog_box, kListRadius, 0);
    lv_obj_set_style_pad_hor(dialog_box, 12, 0);
    lv_obj_set_style_pad_ver(dialog_box, 8, 0);
    lv_obj_clear_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(dialog_box, "自定义校时服务器", false);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    ntp_error_label_ = CreateSettingLabel(dialog_box, "", false);
    lv_obj_set_style_text_font(ntp_error_label_, &phone_font_12, 0);
    lv_obj_set_style_text_color(ntp_error_label_, rodakos_theme_error(), 0);
    lv_obj_align(ntp_error_label_, LV_ALIGN_TOP_LEFT, 0, 22);

    ntp_textarea_ = lv_textarea_create(dialog_box);
    lv_obj_set_size(ntp_textarea_, 152, 36);
    lv_obj_align(ntp_textarea_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_textarea_set_one_line(ntp_textarea_, true);
    lv_textarea_set_max_length(ntp_textarea_, 63);
    lv_textarea_set_text(ntp_textarea_, datetime_server_cache_.c_str());
    lv_textarea_set_placeholder_text(ntp_textarea_, "pool.ntp.org");
    lv_obj_set_style_bg_color(ntp_textarea_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_text_color(ntp_textarea_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(ntp_textarea_, &phone_font_12, 0);
    lv_obj_set_style_border_width(ntp_textarea_, 0, 0);
    lv_obj_set_style_radius(ntp_textarea_, kButtonRadius, 0);

    auto* cancel_btn = CreateSettingButton(dialog_box, "取消", SettingButtonStyle::kSecondary, 60,
                                           kDialogButtonHeight);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 156, 0);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->CloseNtpServerDialog();
    }, LV_EVENT_CLICKED, this);

    auto* save_btn = CreateSettingButton(dialog_box, "保存", SettingButtonStyle::kPrimary, 60,
                                         kDialogButtonHeight);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(save_btn, [](lv_event_t* e) {
        static_cast<SettingsApp*>(lv_event_get_user_data(e))->SubmitNtpServerDialog();
    }, LV_EVENT_CLICKED, this);

    soft_keyboard_.Show(ntp_textarea_, [this]() {
        lv_async_call([](void* user_data) {
            static_cast<SettingsApp*>(user_data)->SubmitNtpServerDialog();
        }, this);
    });
}

void SettingsApp::SubmitNtpServerDialog() {
    if (ntp_textarea_ == nullptr) return;
    const std::string server = TrimServerName(lv_textarea_get_text(ntp_textarea_));
    if (server.empty()) {
        // Keep the dialog open with the reason, instead of closing on an invalid entry.
        lv_label_set_text(ntp_error_label_, "请输入服务器地址");
        return;
    }
    if (!SaveNtpServer(server)) {
        lv_label_set_text(ntp_error_label_, "保存失败，请重试");
        return;
    }
    CloseNtpServerDialog();
}

void SettingsApp::CloseNtpServerDialog() {
    soft_keyboard_.Hide();
    if (ntp_dialog_ != nullptr && lv_obj_is_valid(ntp_dialog_)) {
        lv_obj_delete(ntp_dialog_);
    }
    ntp_dialog_ = nullptr;
    ntp_textarea_ = nullptr;
    ntp_error_label_ = nullptr;
}

void SettingsApp::CloseNtpServerDialogAsync() {
    lv_async_call([](void* user_data) {
        auto* self = static_cast<SettingsApp*>(user_data);
        if (self != nullptr) {
            self->CloseNtpServerDialog();
        }
    }, this);
}
