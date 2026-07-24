#include "apps/clock/clock_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/time_service.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <esp_log.h>

#include <cstdio>
#include <ctime>
#include <memory>

namespace {
constexpr const char* TAG = "ClockApp";
constexpr uint32_t kSyncTimeoutPolls = 20;

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().ReturnHome();
    }
}

void ClockTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<ClockApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->UpdateClock();
    }
}

void SyncTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<ClockApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->UpdateSyncStatus();
    }
}

lv_obj_t* CreateTextLabel(lv_obj_t* parent, const char* text, const lv_font_t* font,
                          lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* CreateCard(lv_obj_t* parent, lv_coord_t y, lv_coord_t height) {
    auto* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, height);
    lv_obj_set_pos(card, 10, y);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

}  // namespace

bool ClockApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    TimeServiceApplySavedTimeZone();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    UpdateClock();
    clock_timer_ = lv_timer_create(ClockTimerCallback, 1000, this);

    ESP_LOGI(TAG, "Clock app created");
    return true;
}

void ClockApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            DestroyUi();
        }
    }

    context_ = nullptr;
    ui_ = nullptr;
    sync_in_progress_ = false;
    sync_poll_count_ = 0;
}

bool ClockApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    const bool was_hidden = root_ != nullptr && lv_obj_is_valid(root_) &&
                            lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    const bool restart_sync_timer = sync_in_progress_;

    DestroyUi();
    CreateUi();
    UpdateClock();
    if (sync_status_label_ != nullptr && sync_in_progress_) {
        lv_label_set_text(sync_status_label_, "Syncing...");
        lv_obj_set_style_text_color(sync_status_label_, rodakos_theme_primary(), 0);
    }
    if (was_hidden && root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    clock_timer_ = lv_timer_create(ClockTimerCallback, 1000, this);
    if (restart_sync_timer) {
        sync_timer_ = lv_timer_create(SyncTimerCallback, 1000, this);
    }
    return true;
}

void ClockApp::DestroyUi() {
    if (clock_timer_ != nullptr) {
        lv_timer_delete(clock_timer_);
        clock_timer_ = nullptr;
    }
    if (sync_timer_ != nullptr) {
        lv_timer_delete(sync_timer_);
        sync_timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    ResetUiPointers();
}

void ClockApp::ResetUiPointers() {
    root_ = nullptr;
    time_label_ = nullptr;
    seconds_label_ = nullptr;
    date_label_ = nullptr;
    timezone_label_ = nullptr;
    server_label_ = nullptr;
    sync_status_label_ = nullptr;
    time_row_ = nullptr;
}

void ClockApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Clock", [](lv_event_t* e) {
        auto* self = static_cast<ClockApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, [](lv_event_t* e) {
        auto* self = static_cast<ClockApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this);

    auto* face = lv_obj_create(root_);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, 300, 92);
    lv_obj_set_pos(face, 10, 48);
    lv_obj_set_style_bg_color(face, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(face, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(face, 8, 0);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_SCROLLABLE);

    time_row_ = lv_obj_create(face);
    lv_obj_remove_style_all(time_row_);
    lv_obj_set_size(time_row_, 170, 38);
    lv_obj_align(time_row_, LV_ALIGN_LEFT_MID, 16, -10);
    lv_obj_set_flex_flow(time_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row_, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_gap(time_row_, 4, 0);
    lv_obj_clear_flag(time_row_, LV_OBJ_FLAG_SCROLLABLE);

    time_label_ = CreateTextLabel(time_row_, "--:--", &lv_font_montserrat_26,
                                  rodakos_theme_text_primary());
    lv_obj_set_size(time_label_, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_text_align(time_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(time_label_, LV_LABEL_LONG_CLIP);

    seconds_label_ = CreateTextLabel(time_row_, ":--", &phone_font_18,
                                     rodakos_theme_primary());
    lv_obj_set_size(seconds_label_, LV_SIZE_CONTENT, 24);
    lv_obj_set_style_text_align(seconds_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(seconds_label_, LV_LABEL_LONG_CLIP);

    date_label_ = CreateTextLabel(face, "Waiting for sync", &phone_font_12,
                                  rodakos_theme_text_secondary());
    lv_obj_align(date_label_, LV_ALIGN_BOTTOM_LEFT, 16, -12);

    auto* tz_card = CreateCard(root_, 150, 34);
    auto* tz_icon = CreateTextLabel(tz_card, FONT_AWESOME_GLOBE, PhoneIconFont(),
                                    rodakos_theme_primary());
    lv_obj_align(tz_icon, LV_ALIGN_LEFT_MID, 0, 0);

    timezone_label_ = CreateTextLabel(tz_card, "", &phone_font_12,
                                      rodakos_theme_text_primary());
    lv_obj_set_width(timezone_label_, 250);
    lv_label_set_long_mode(timezone_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(timezone_label_, LV_ALIGN_LEFT_MID, 28, 0);

    auto* server_card = CreateCard(root_, 190, 34);
    auto* server_icon = CreateTextLabel(server_card, FONT_AWESOME_CLOUD, PhoneIconFont(),
                                        rodakos_theme_primary());
    lv_obj_align(server_icon, LV_ALIGN_LEFT_MID, 0, 0);

    server_label_ = CreateTextLabel(server_card, "", &phone_font_12,
                                    rodakos_theme_text_primary());
    lv_obj_set_width(server_label_, 204);
    lv_label_set_long_mode(server_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(server_label_, LV_ALIGN_LEFT_MID, 28, 0);

    auto* sync_btn = lv_btn_create(server_card);
    lv_obj_remove_style_all(sync_btn);
    lv_obj_set_size(sync_btn, 40, 26);
    lv_obj_align(sync_btn, LV_ALIGN_RIGHT_MID, 0, 0);
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
        auto* self = static_cast<ClockApp*>(lv_event_get_user_data(e));
        self->StartSync();
    }, LV_EVENT_CLICKED, this);

    sync_status_label_ = CreateTextLabel(root_, "Not synced", &phone_font_12,
                                         rodakos_theme_text_tertiary());
    lv_obj_set_width(sync_status_label_, 300);
    lv_obj_set_style_text_align(sync_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sync_status_label_, LV_ALIGN_BOTTOM_MID, 0, -3);
}

void ClockApp::UpdateClock() {
    if (time_label_ == nullptr || seconds_label_ == nullptr || date_label_ == nullptr) {
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm timeinfo = {};
    localtime_r(&now, &timeinfo);

    char time_text[8] = {};
    char seconds_text[8] = {};
    char date_text[48] = {};

    if (!TimeIsValid()) {
        std::snprintf(time_text, sizeof(time_text), "--:--");
        std::snprintf(seconds_text, sizeof(seconds_text), ":--");
        std::snprintf(date_text, sizeof(date_text), "Waiting for sync");
    } else {
        std::snprintf(time_text, sizeof(time_text), "%02d:%02d",
                      timeinfo.tm_hour, timeinfo.tm_min);
        std::snprintf(seconds_text, sizeof(seconds_text), ":%02d", timeinfo.tm_sec);
        std::strftime(date_text, sizeof(date_text), "%a, %Y-%m-%d", &timeinfo);
    }

    lv_label_set_text(time_label_, time_text);
    lv_label_set_text(seconds_label_, seconds_text);
    lv_label_set_text(date_label_, date_text);

    if (timezone_label_ != nullptr) {
        size_t count = 0;
        const auto* zones = TimeServiceTimeZones(&count);
        const size_t index = TimeServiceFindTimeZoneIndex(TimeServiceLoadTimeZone());
        const char* label = (index < count) ? zones[index].label : "Custom time zone";
        lv_label_set_text_fmt(timezone_label_, "Time zone: %s", label);
    }
    if (server_label_ != nullptr) {
        lv_label_set_text_fmt(server_label_, "Server: %s", TimeServiceLoadNtpServer().c_str());
    }
    if (time_row_ != nullptr) {
        lv_obj_update_layout(time_row_);
    }
}

void ClockApp::UpdateSyncStatus() {
    if (!sync_in_progress_) {
        return;
    }

    sync_poll_count_++;
    const TimeSyncStatus status = TimeServiceGetSyncStatus();
    if (status == TimeSyncStatus::kCompleted) {
        sync_in_progress_ = false;
        if (sync_timer_ != nullptr) {
            lv_timer_delete(sync_timer_);
            sync_timer_ = nullptr;
        }
        lv_label_set_text(sync_status_label_, "Synced");
        lv_obj_set_style_text_color(sync_status_label_, rodakos_theme_success(), 0);
        UpdateClock();
        ui_->ShowToastUnlocked("Time synced");
        ESP_LOGI(TAG, "SNTP sync completed");
        return;
    }

    if (sync_poll_count_ >= kSyncTimeoutPolls) {
        sync_in_progress_ = false;
        if (sync_timer_ != nullptr) {
            lv_timer_delete(sync_timer_);
            sync_timer_ = nullptr;
        }
        lv_label_set_text(sync_status_label_, "Sync timeout");
        lv_obj_set_style_text_color(sync_status_label_, rodakos_theme_warning(), 0);
        ui_->ShowToastUnlocked("Sync timeout");
        ESP_LOGW(TAG, "SNTP sync timed out");
        return;
    }

    char status_text[40] = {};
    std::snprintf(status_text, sizeof(status_text), "Syncing... %lus",
                  static_cast<unsigned long>(sync_poll_count_));
    lv_label_set_text(sync_status_label_, status_text);
}

void ClockApp::StartSync() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr || wifi->GetStatus() != WiFiStatus::kConnected) {
        lv_label_set_text(sync_status_label_, "WiFi not connected");
        lv_obj_set_style_text_color(sync_status_label_, rodakos_theme_warning(), 0);
        ui_->ShowToastUnlocked("Connect WiFi first");
        return;
    }

    if (sync_timer_ != nullptr) {
        lv_timer_delete(sync_timer_);
        sync_timer_ = nullptr;
    }

    if (!TimeServiceStartSavedSync()) {
        lv_label_set_text(sync_status_label_, "Sync failed");
        lv_obj_set_style_text_color(sync_status_label_, rodakos_theme_warning(), 0);
        return;
    }

    sync_in_progress_ = true;
    sync_poll_count_ = 0;
    lv_label_set_text(sync_status_label_, "Syncing...");
    lv_obj_set_style_text_color(sync_status_label_, rodakos_theme_primary(), 0);
    sync_timer_ = lv_timer_create(SyncTimerCallback, 1000, this);
    ui_->ShowToastUnlocked("Time sync started");
}

void ClockApp::NavigateHome() {
    lv_async_call(DeferReturnHome, context_);
}

bool ClockApp::TimeIsValid() const {
    return TimeServiceTimeIsValid();
}

void RegisterClockApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "clock",
        .title = "Clock",
        .icon = FONT_AWESOME_CLOCK,
        .category = PhoneAppCategory::kTools,
        .capabilities = PhoneCapability::kNetwork,
        .show_on_home = true,
        .aliases = {"time", "ntp", "时钟", "时间"},
        .create = []() { return std::make_unique<ClockApp>(); },
    });
}
