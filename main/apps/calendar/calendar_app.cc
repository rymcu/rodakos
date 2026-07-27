#include "apps/calendar/calendar_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/time_service.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <memory>

namespace {
constexpr const char* TAG = "CalendarApp";
constexpr lv_coord_t kGridOriginX = 10;
constexpr lv_coord_t kGridOriginY = 92;
constexpr lv_coord_t kGridColumnStep = 43;
constexpr lv_coord_t kGridRowStep = 19;
constexpr lv_coord_t kDayCellWidth = 39;
constexpr lv_coord_t kDayCellHeight = 17;

constexpr std::array<const char*, 12> kMonthNames = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
};

constexpr std::array<const char*, 7> kWeekdayNames = {
    "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN",
};

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month) {
    constexpr std::array<int, 12> kDays = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    if (month == 2 && IsLeapYear(year)) {
        return 29;
    }
    return kDays[static_cast<size_t>(month - 1)];
}

int MondayFirstWeekday(int year, int month) {
    std::tm first = {};
    first.tm_year = year - 1900;
    first.tm_mon = month - 1;
    first.tm_mday = 1;
    std::mktime(&first);
    return (first.tm_wday + 6) % 7;
}

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().ReturnHome();
    }
}

}  // namespace

void CalendarApp::RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<CalendarApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->RefreshToday();
    }
}

void CalendarApp::DayButtonCallback(lv_event_t* event) {
    auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->SelectDay(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    }
}

bool CalendarApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    TimeServiceApplySavedTimeZone();

    std::tm now = {};
    if (ReadLocalTime(&now)) {
        display_year_ = now.tm_year + 1900;
        display_month_ = now.tm_mon + 1;
        selected_day_ = now.tm_mday;
        has_valid_clock_ = true;
    }

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    RenderMonth();
    RenderSelection();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, 60 * 1000, this);

    ESP_LOGI(TAG, "Calendar app created, clock_valid=%d", has_valid_clock_ ? 1 : 0);
    return true;
}

void CalendarApp::OnResume() {
    paused_ = false;
    if (ui_ == nullptr) {
        return;
    }
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return;
    }
    if (refresh_timer_ != nullptr) {
        lv_timer_resume(refresh_timer_);
        lv_timer_reset(refresh_timer_);
    }
    RefreshToday();
}

void CalendarApp::OnPause() {
    paused_ = true;
    if (ui_ == nullptr) {
        return;
    }
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return;
    }
    if (refresh_timer_ != nullptr) {
        lv_timer_pause(refresh_timer_);
    }
}

void CalendarApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            DestroyUi();
        }
    }

    context_ = nullptr;
    ui_ = nullptr;
    paused_ = false;
}

bool CalendarApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    const bool was_hidden = root_ != nullptr && lv_obj_is_valid(root_) &&
                            lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    const bool was_paused = paused_;

    DestroyUi();
    CreateUi();
    RenderMonth();
    RenderSelection();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, 60 * 1000, this);
    if (was_paused) {
        lv_timer_pause(refresh_timer_);
    }
    if (was_hidden && root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    return true;
}

void CalendarApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Calendar", [](lv_event_t* event) {
        auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(event));
        if (self != nullptr) {
            self->NavigateHome();
        }
    }, [](lv_event_t* event) {
        auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(event));
        if (self != nullptr) {
            self->NavigateHome();
        }
    }, this);

    previous_button_ = RodakosCreateHeaderIconButton(root_, FONT_AWESOME_ANGLE_LEFT);
    lv_obj_set_pos(previous_button_, 8, 45);
    lv_obj_add_event_cb(previous_button_, [](lv_event_t* event) {
        auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(event));
        if (self != nullptr) {
            self->NavigateMonth(-1);
        }
    }, LV_EVENT_CLICKED, this);

    month_label_ = lv_label_create(root_);
    lv_obj_set_size(month_label_, 140, 26);
    lv_obj_set_pos(month_label_, 50, 45);
    lv_obj_set_style_text_font(month_label_, &phone_font_14, 0);
    lv_obj_set_style_text_color(month_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_align(month_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(month_label_, LV_LABEL_LONG_CLIP);

    today_button_ = PhoneCreateTextButton(*ui_, root_, "Today", 54, 26);
    lv_obj_set_pos(today_button_, 194, 45);
    lv_obj_add_event_cb(today_button_, [](lv_event_t* event) {
        auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(event));
        if (self != nullptr) {
            self->SelectToday();
        }
    }, LV_EVENT_CLICKED, this);

    next_button_ = RodakosCreateHeaderIconButton(root_, FONT_AWESOME_ANGLE_RIGHT);
    lv_obj_set_pos(next_button_, 262, 45);
    lv_obj_add_event_cb(next_button_, [](lv_event_t* event) {
        auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(event));
        if (self != nullptr) {
            self->NavigateMonth(1);
        }
    }, LV_EVENT_CLICKED, this);

    for (size_t column = 0; column < kWeekdayNames.size(); ++column) {
        auto* label = lv_label_create(root_);
        lv_label_set_text(label, kWeekdayNames[column]);
        lv_obj_set_size(label, kDayCellWidth, 16);
        lv_obj_set_pos(label, kGridOriginX + static_cast<lv_coord_t>(column) * kGridColumnStep, 75);
        lv_obj_set_style_text_font(label, &phone_font_12, 0);
        lv_obj_set_style_text_color(label, rodakos_theme_text_tertiary(), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

    for (size_t index = 0; index < kDayCellCount; ++index) {
        const auto row = index / 7;
        const auto column = index % 7;
        auto* button = lv_btn_create(root_);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, kDayCellWidth, kDayCellHeight);
        lv_obj_set_pos(button,
                       kGridOriginX + static_cast<lv_coord_t>(column) * kGridColumnStep,
                       kGridOriginY + static_cast<lv_coord_t>(row) * kGridRowStep);
        lv_obj_set_style_radius(button, 5, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_bg_color(button, rodakos_theme_primary(), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(button, DayButtonCallback, LV_EVENT_CLICKED, this);

        auto* label = lv_label_create(button);
        lv_obj_set_style_text_font(label, &phone_font_12, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        day_buttons_[index] = button;
        day_labels_[index] = label;
    }

    selected_label_ = lv_label_create(root_);
    lv_obj_set_size(selected_label_, 300, 16);
    lv_obj_set_pos(selected_label_, 10, 211);
    lv_obj_set_style_text_font(selected_label_, &phone_font_12, 0);
    lv_obj_set_style_text_color(selected_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_align(selected_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(selected_label_, LV_LABEL_LONG_CLIP);

    sync_status_label_ = lv_label_create(root_);
    lv_obj_set_size(sync_status_label_, 300, 14);
    lv_obj_set_pos(sync_status_label_, 10, 226);
    lv_obj_set_style_text_font(sync_status_label_, &phone_font_12, 0);
    lv_obj_set_style_text_align(sync_status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(sync_status_label_, LV_LABEL_LONG_CLIP);
}

void CalendarApp::DestroyUi() {
    if (refresh_timer_ != nullptr) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    ResetUiPointers();
}

void CalendarApp::ResetUiPointers() {
    root_ = nullptr;
    month_label_ = nullptr;
    selected_label_ = nullptr;
    sync_status_label_ = nullptr;
    today_button_ = nullptr;
    previous_button_ = nullptr;
    next_button_ = nullptr;
    day_buttons_.fill(nullptr);
    day_labels_.fill(nullptr);
}

void CalendarApp::RenderMonth() {
    if (month_label_ == nullptr) {
        return;
    }

    std::tm now = {};
    const bool has_valid_now = ReadLocalTime(&now);
    if (!has_valid_now) {
        lv_label_set_text(month_label_, "Waiting for sync");
        for (size_t index = 0; index < kDayCellCount; ++index) {
            if (day_buttons_[index] != nullptr) {
                lv_obj_add_flag(day_buttons_[index], LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }

    lv_label_set_text_fmt(month_label_, "%s %d", kMonthNames[static_cast<size_t>(display_month_ - 1)],
                          display_year_);

    const int first_weekday = MondayFirstWeekday(display_year_, display_month_);
    const int days = DaysInMonth(display_year_, display_month_);
    for (size_t index = 0; index < kDayCellCount; ++index) {
        auto* button = day_buttons_[index];
        auto* label = day_labels_[index];
        if (button == nullptr || label == nullptr) {
            continue;
        }

        const int day = static_cast<int>(index) - first_weekday + 1;
        if (day < 1 || day > days) {
            lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
        const bool selected = day == selected_day_;
        const bool is_today = has_valid_now && now.tm_year + 1900 == display_year_ &&
                              now.tm_mon + 1 == display_month_ && now.tm_mday == day;
        const bool weekend = (index % 7) >= 5;
        lv_obj_set_style_bg_color(button,
                                  selected ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_border_width(button, is_today && !selected ? 2 : 0, 0);
        lv_obj_set_style_border_color(button, rodakos_theme_primary(), 0);
        lv_obj_set_style_text_color(
            label,
            selected ? lv_color_white()
                     : (weekend ? rodakos_theme_warning() : rodakos_theme_text_primary()),
            0);
        lv_label_set_text_fmt(label, "%d", day);
    }
}

void CalendarApp::RenderSelection() {
    if (selected_label_ == nullptr || sync_status_label_ == nullptr) {
        return;
    }

    if (!has_valid_clock_ || !TimeServiceTimeIsValid()) {
        lv_label_set_text(selected_label_, "Set the clock to view dates");
        lv_label_set_text(sync_status_label_, "Clock not synchronized");
        lv_obj_set_style_text_color(sync_status_label_, rodakos_theme_warning(), 0);
        return;
    }

    std::tm selected = {};
    selected.tm_year = display_year_ - 1900;
    selected.tm_mon = display_month_ - 1;
    selected.tm_mday = selected_day_;
    std::mktime(&selected);

    const char* weekday = kWeekdayNames[static_cast<size_t>((selected.tm_wday + 6) % 7)];
    lv_label_set_text_fmt(selected_label_, "Selected: %s %s %d, %d", weekday,
                          kMonthNames[static_cast<size_t>(display_month_ - 1)], selected_day_,
                          display_year_);

    lv_label_set_text(sync_status_label_, "Clock synchronized");
    lv_obj_set_style_text_color(sync_status_label_, rodakos_theme_success(), 0);
}

void CalendarApp::RefreshToday() {
    std::tm now = {};
    if (ReadLocalTime(&now)) {
        if (!has_valid_clock_) {
            display_year_ = now.tm_year + 1900;
            display_month_ = now.tm_mon + 1;
            selected_day_ = now.tm_mday;
        }
        has_valid_clock_ = true;
    } else {
        has_valid_clock_ = false;
    }
    RenderMonth();
    RenderSelection();
}

void CalendarApp::NavigateMonth(int delta) {
    if (!has_valid_clock_) {
        return;
    }

    display_month_ += delta;
    if (display_month_ < 1) {
        display_month_ = 12;
        --display_year_;
    } else if (display_month_ > 12) {
        display_month_ = 1;
        ++display_year_;
    }
    selected_day_ = std::min(selected_day_, DaysInMonth(display_year_, display_month_));
    RenderMonth();
    RenderSelection();
}

void CalendarApp::SelectToday() {
    std::tm now = {};
    if (!ReadLocalTime(&now)) {
        return;
    }
    display_year_ = now.tm_year + 1900;
    display_month_ = now.tm_mon + 1;
    selected_day_ = now.tm_mday;
    has_valid_clock_ = true;
    RenderMonth();
    RenderSelection();
}

void CalendarApp::SelectDay(lv_obj_t* target) {
    if (!has_valid_clock_) {
        return;
    }

    for (size_t index = 0; index < day_buttons_.size(); ++index) {
        if (day_buttons_[index] != target) {
            continue;
        }
        const int first_weekday = MondayFirstWeekday(display_year_, display_month_);
        const int day = static_cast<int>(index) - first_weekday + 1;
        if (day >= 1 && day <= DaysInMonth(display_year_, display_month_)) {
            selected_day_ = day;
            RenderMonth();
            RenderSelection();
        }
        return;
    }
}

void CalendarApp::NavigateHome() {
    lv_async_call(DeferReturnHome, context_);
}

bool CalendarApp::ReadLocalTime(std::tm* result) const {
    if (result == nullptr || !TimeServiceTimeIsValid()) {
        return false;
    }
    const std::time_t now = std::time(nullptr);
    return localtime_r(&now, result) != nullptr;
}

void RegisterCalendarApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "calendar",
        .title = "Calendar",
        .icon = FONT_AWESOME_CALENDAR,
        .category = PhoneAppCategory::kTools,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = true,
        .aliases = {"cal", "agenda", "日历", "日程"},
        .create = []() { return std::make_unique<CalendarApp>(); },
    });
}
