#include "apps/calendar/calendar_app.h"

#include "phone_os/lunar_calendar.h"
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
#include <cstdlib>
#include <ctime>
#include <memory>

LV_FONT_DECLARE(font_puhui_lunar_12_4);

namespace {
constexpr const char* TAG = "CalendarApp";
constexpr uint32_t kClockRefreshMs = 60 * 1000;
constexpr uint32_t kSyncPollMs = 1000;
constexpr int32_t kSwipeThreshold = 40;

constexpr lv_coord_t kCellWidth = 44;
constexpr lv_coord_t kGridX = 6;
constexpr lv_coord_t kWeekdayY = 41;
constexpr lv_coord_t kGridY = 58;
// Rows share the 182 px below the weekday row: 6 weeks get 30 px, 5 get 36, 4 get 44.
constexpr lv_coord_t kGridHeight = 240 - kGridY;
constexpr lv_coord_t kMaxRowHeight = 44;

constexpr std::array<const char*, 7> kWeekdayShort = {"一", "二", "三", "四", "五", "六", "日"};
constexpr std::array<const char*, 7> kWeekdayLong = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};

bool IsLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month) {
    constexpr std::array<int, 12> kDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && IsLeapYear(year) ? 29 : kDays[static_cast<size_t>(month - 1)];
}

// Days since 1970-01-01 for a proleptic Gregorian date; avoids mktime and the time zone.
int64_t DayNumber(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yoe = year - era * 400;
    const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// 0 = Monday.
int MondayWeekday(int year, int month, int day) {
    return static_cast<int>(((DayNumber(year, month, day) % 7) + 7 + 3) % 7);
}

int IsoWeek(int year, int month, int day) {
    const int64_t n = DayNumber(year, month, day);
    const int64_t thursday = n - MondayWeekday(year, month, day) + 3;
    // Year that contains this week's Thursday.
    int y = year;
    if (thursday < DayNumber(y, 1, 1)) --y;
    if (thursday >= DayNumber(y + 1, 1, 1)) ++y;
    return static_cast<int>((thursday - DayNumber(y, 1, 1)) / 7 + 1);
}

void ShiftDate(int& year, int& month, int& day, int delta) {
    day += delta;
    while (day < 1) {
        if (--month < 1) { month = 12; --year; }
        day += DaysInMonth(year, month);
    }
    while (day > DaysInMonth(year, month)) {
        day -= DaysInMonth(year, month);
        if (++month > 12) { month = 1; ++year; }
    }
}

lv_obj_t* CreatePlain(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

void SetHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// Text under the day number: festival, then solar term, then the month name on 初一, else the day.
const char* CellLunarText(int year, int month, int day, bool& accent, char* buffer, size_t size) {
    accent = false;
    rodakos::LunarDate lunar;
    if (!rodakos::SolarToLunar(year, month, day, lunar)) return "";
    if (const char* festival = rodakos::FestivalName(year, month, day, lunar)) {
        accent = true;
        return festival;
    }
    if (lunar.solar_term >= 0) {
        accent = true;
        return rodakos::SolarTermName(lunar.solar_term);
    }
    if (lunar.day == 1) {
        std::snprintf(buffer, size, "%s%s月", lunar.leap ? "闰" : "", rodakos::LunarMonthName(lunar.month));
        return buffer;
    }
    return rodakos::LunarDayName(lunar.day);
}

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().ReturnHome();
}

}  // namespace

void CalendarApp::RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<CalendarApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->RefreshToday();
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
    if (!lock.locked()) return false;

    view_ = has_valid_clock_ ? View::kMonth : View::kUnsynced;
    CreateUi();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, has_valid_clock_ ? kClockRefreshMs : kSyncPollMs, this);
    ESP_LOGI(TAG, "Calendar app created, clock_valid=%d", has_valid_clock_ ? 1 : 0);
    return true;
}

void CalendarApp::OnResume() {
    paused_ = false;
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return;
    if (refresh_timer_ != nullptr) {
        lv_timer_resume(refresh_timer_);
        lv_timer_reset(refresh_timer_);
    }
    RefreshToday();
}

void CalendarApp::OnPause() {
    paused_ = true;
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return;
    if (refresh_timer_ != nullptr) lv_timer_pause(refresh_timer_);
}

void CalendarApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (context_ != nullptr) lv_async_call_cancel(DeferReturnHome, context_);
            if (refresh_timer_ != nullptr) {
                lv_timer_delete(refresh_timer_);
                refresh_timer_ = nullptr;
            }
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
    if (!lock.locked()) return false;
    const bool hidden = root_ != nullptr && lv_obj_is_valid(root_) && lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    DestroyUi();
    CreateUi();
    if (hidden && root_ != nullptr) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    return true;
}

bool CalendarApp::OnBackRequested() {
    NavigateBack();
    return true;
}

void CalendarApp::CreateUi() {
    root_ = CreatePlain(ui_->screen());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    // Own header: the month view puts ‹ month › and 今天 in it, which CreateAppHeader cannot.
    header_ = CreatePlain(root_);
    lv_obj_set_size(header_, 320, kRodakosAppHeaderHeight);
    lv_obj_set_style_bg_color(header_, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(header_, LV_OPA_COVER, 0);

    back_button_ = RodakosCreateHeaderIconButton(header_, ICON_ARROW_LEFT);
    lv_obj_set_pos(back_button_, 10, 7);
    lv_obj_add_event_cb(back_button_, [](lv_event_t* e) {
        static_cast<CalendarApp*>(lv_event_get_user_data(e))->NavigateBack();
    }, LV_EVENT_CLICKED, this);

    title_label_ = CreateText(header_, "日历", &phone_font_18, rodakos_theme_text_primary());
    lv_label_set_long_mode(title_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);

    previous_button_ = RodakosCreateHeaderIconButton(header_, ICON_CHEVRON_LEFT);
    lv_obj_set_size(previous_button_, 32, 26);
    lv_obj_add_event_cb(previous_button_, [](lv_event_t* e) {
        auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(e));
        self->view_ == View::kDay ? self->NavigateDay(-1) : self->NavigateMonth(-1);
    }, LV_EVENT_CLICKED, this);
    next_button_ = RodakosCreateHeaderIconButton(header_, ICON_CHEVRON_RIGHT);
    lv_obj_set_size(next_button_, 32, 26);
    lv_obj_add_event_cb(next_button_, [](lv_event_t* e) {
        auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(e));
        self->view_ == View::kDay ? self->NavigateDay(1) : self->NavigateMonth(1);
    }, LV_EVENT_CLICKED, this);

    today_button_ = RodakosCreateHeaderIconButton(header_, "");
    lv_obj_set_size(today_button_, 44, 26);
    lv_obj_set_pos(today_button_, 266, 7);
    today_label_ = lv_obj_get_child(today_button_, 0);
    lv_label_set_text(today_label_, "今天");
    lv_obj_set_style_text_font(today_label_, &phone_font_14, 0);
    lv_obj_add_event_cb(today_button_, [](lv_event_t* e) {
        static_cast<CalendarApp*>(lv_event_get_user_data(e))->SelectToday();
    }, LV_EVENT_CLICKED, this);

    CreateMonthView();
    CreateDayView();
    CreateUnsyncedView();
    ShowView(view_);
}

void CalendarApp::DestroyUi() {
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    root_ = header_ = back_button_ = title_label_ = nullptr;
    previous_button_ = next_button_ = today_button_ = today_label_ = nullptr;
    month_view_ = day_view_ = unsynced_view_ = nullptr;
    weekday_labels_.fill(nullptr);
    cells_.fill(Cell{});
    day_number_label_ = day_relative_label_ = day_lunar_label_ = nullptr;
    day_detail_labels_.fill(nullptr);
    unsynced_hint_ = sync_button_ = sync_button_label_ = nullptr;
    swipe_tracking_ = false;
}

void CalendarApp::CreateMonthView() {
    month_view_ = CreatePlain(root_);
    lv_obj_set_size(month_view_, 320, 200);
    lv_obj_set_pos(month_view_, 0, kRodakosAppHeaderHeight);
    lv_obj_add_flag(month_view_, LV_OBJ_FLAG_CLICKABLE);
    for (auto code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
        lv_obj_add_event_cb(month_view_, [](lv_event_t* e) {
            static_cast<CalendarApp*>(lv_event_get_user_data(e))->HandleSwipe(e);
        }, code, this);
    }

    for (size_t column = 0; column < 7; ++column) {
        auto* label = CreateText(month_view_, kWeekdayShort[column], &phone_font_12,
                                 column >= 5 ? rodakos_theme_warning() : rodakos_theme_text_tertiary());
        lv_obj_set_size(label, kCellWidth, 16);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(label, kGridX + static_cast<lv_coord_t>(column) * kCellWidth,
                       kWeekdayY - kRodakosAppHeaderHeight);
        weekday_labels_[column] = label;
    }

    for (size_t index = 0; index < kDayCellCount; ++index) {
        Cell& cell = cells_[index];
        cell.button = CreatePlain(month_view_);
        lv_obj_add_flag(cell.button, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE));
        lv_obj_set_style_radius(cell.button, 7, 0);
        lv_obj_set_style_bg_color(cell.button, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell.button, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(cell.button, rodakos_theme_primary(), 0);
        lv_obj_set_user_data(cell.button, reinterpret_cast<void*>(index));
        lv_obj_add_event_cb(cell.button, [](lv_event_t* e) {
            auto* self = static_cast<CalendarApp*>(lv_event_get_user_data(e));
            if (self->swipe_direction_ != 0) return;
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            self->OpenCell(reinterpret_cast<size_t>(lv_obj_get_user_data(target)));
        }, LV_EVENT_CLICKED, this);

        cell.day = CreateText(cell.button, "", &phone_font_14, rodakos_theme_text_primary());
        cell.lunar = CreateText(cell.button, "", &font_puhui_lunar_12_4, rodakos_theme_text_tertiary());
    }
}

void CalendarApp::CreateDayView() {
    day_view_ = CreatePlain(root_);
    lv_obj_set_size(day_view_, 320, 200);
    lv_obj_set_pos(day_view_, 0, kRodakosAppHeaderHeight);
    lv_obj_add_flag(day_view_, LV_OBJ_FLAG_CLICKABLE);
    for (auto code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
        lv_obj_add_event_cb(day_view_, [](lv_event_t* e) {
            static_cast<CalendarApp*>(lv_event_get_user_data(e))->HandleSwipe(e);
        }, code, this);
    }

    auto* hero = CreatePlain(day_view_);
    lv_obj_set_size(hero, 92, 92);
    lv_obj_set_pos(hero, 10, 8);
    lv_obj_set_style_bg_color(hero, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hero, 10, 0);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_CLICKABLE);
    day_number_label_ = CreateText(hero, "", &lv_font_montserrat_26, rodakos_theme_text_primary());
    lv_obj_align(day_number_label_, LV_ALIGN_TOP_MID, 0, 18);
    day_relative_label_ = CreateText(hero, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_align(day_relative_label_, LV_ALIGN_TOP_MID, 0, 58);

    day_lunar_label_ = CreateText(day_view_, "", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_pos(day_lunar_label_, 114, 10);
    for (size_t i = 0; i < day_detail_labels_.size(); ++i) {
        auto* label = CreateText(day_view_, "", &phone_font_14, rodakos_theme_text_secondary());
        lv_obj_set_width(label, 196);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(label, 114, static_cast<lv_coord_t>(36 + i * 22));
        day_detail_labels_[i] = label;
    }

    auto* section = CreateText(day_view_, "日程", &phone_font_14, rodakos_theme_text_tertiary());
    lv_obj_set_pos(section, 12, 110);
    auto* placeholder = CreatePlain(day_view_);
    lv_obj_set_size(placeholder, 300, 56);
    lv_obj_set_pos(placeholder, 10, 134);
    lv_obj_set_style_radius(placeholder, 8, 0);
    lv_obj_set_style_border_color(placeholder, rodakos_theme_border(), 0);
    lv_obj_set_style_border_width(placeholder, 1, 0);
    lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_CLICKABLE);
    auto* hint = CreateText(placeholder, "日程服务接入后显示在这里", &phone_font_14, rodakos_theme_text_tertiary());
    lv_obj_center(hint);
}

void CalendarApp::CreateUnsyncedView() {
    unsynced_view_ = CreatePlain(root_);
    lv_obj_set_size(unsynced_view_, 320, 200);
    lv_obj_set_pos(unsynced_view_, 0, kRodakosAppHeaderHeight);
    auto* icon = CreateText(unsynced_view_, ICON_CALENDAR, PhoneIconFontLarge(), rodakos_theme_text_tertiary());
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 26);
    auto* title = CreateText(unsynced_view_, "时间尚未校准", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 66);
    unsynced_hint_ = CreateText(unsynced_view_, "", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_align(unsynced_hint_, LV_ALIGN_TOP_MID, 0, 90);

    sync_button_ = lv_btn_create(unsynced_view_);
    lv_obj_remove_style_all(sync_button_);
    lv_obj_set_size(sync_button_, 140, 38);
    lv_obj_align(sync_button_, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_set_style_radius(sync_button_, 8, 0);
    lv_obj_set_style_bg_color(sync_button_, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(sync_button_, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(sync_button_, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_flex_flow(sync_button_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sync_button_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sync_button_, 6, 0);
    CreateText(sync_button_, ICON_REFRESH, PhoneIconFont(), rodakos_theme_on_primary());
    sync_button_label_ = CreateText(sync_button_, "立即校时", &phone_font_14, rodakos_theme_on_primary());
    lv_obj_add_event_cb(sync_button_, [](lv_event_t* e) {
        static_cast<CalendarApp*>(lv_event_get_user_data(e))->StartSync();
    }, LV_EVENT_CLICKED, this);
}

void CalendarApp::ShowView(View view) {
    view_ = view;
    if (root_ == nullptr) return;
    SetHidden(month_view_, view != View::kMonth);
    SetHidden(day_view_, view != View::kDay);
    SetHidden(unsynced_view_, view != View::kUnsynced);
    SetHidden(previous_button_, view == View::kUnsynced);
    SetHidden(next_button_, view == View::kUnsynced);
    SetHidden(today_button_, view != View::kMonth);

    if (view == View::kMonth) {
        lv_obj_set_size(title_label_, 132, 22);
        lv_obj_set_pos(title_label_, 94, 9);
        lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(previous_button_, 58, 7);
        lv_obj_set_pos(next_button_, 230, 7);
        RenderMonth();
    } else if (view == View::kDay) {
        lv_obj_set_size(title_label_, 164, 22);
        lv_obj_set_pos(title_label_, 58, 9);
        lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(previous_button_, 236, 7);
        lv_obj_set_pos(next_button_, 278, 7);
        RenderDay();
    } else {
        lv_obj_set_size(title_label_, 208, 22);
        lv_obj_set_pos(title_label_, 56, 9);
        lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(title_label_, "日历");
        RenderUnsynced();
    }
}

void CalendarApp::RenderMonth() {
    if (month_view_ == nullptr) return;
    lv_label_set_text_fmt(title_label_, "%d 年 %d 月", display_year_, display_month_);

    std::tm now = {};
    const bool has_now = ReadLocalTime(&now);
    const int today_year = has_now ? now.tm_year + 1900 : 0;
    const int today_month = has_now ? now.tm_mon + 1 : 0;
    const int today_day = has_now ? now.tm_mday : 0;
    const bool this_month = display_year_ == today_year && display_month_ == today_month;
    lv_obj_set_style_bg_color(today_button_, this_month ? rodakos_theme_bg_tertiary() : rodakos_theme_primary(), 0);
    lv_obj_set_style_text_color(today_label_, this_month ? rodakos_theme_text_primary() : rodakos_theme_on_primary(), 0);

    const int first = MondayWeekday(display_year_, display_month_, 1);
    const int days = DaysInMonth(display_year_, display_month_);
    const int weeks = (first + days + 6) / 7;
    const lv_coord_t row_height = std::min<lv_coord_t>(kMaxRowHeight, kGridHeight / weeks);
    const int prev_month = display_month_ == 1 ? 12 : display_month_ - 1;
    const int prev_year = display_month_ == 1 ? display_year_ - 1 : display_year_;
    const int prev_days = DaysInMonth(prev_year, prev_month);

    char buffer[16];
    for (size_t index = 0; index < kDayCellCount; ++index) {
        Cell& cell = cells_[index];
        const int row = static_cast<int>(index) / 7;
        const int column = static_cast<int>(index) % 7;
        if (row >= weeks) {
            SetHidden(cell.button, true);
            continue;
        }
        SetHidden(cell.button, false);
        int day = static_cast<int>(index) - first + 1;
        cell.year = display_year_;
        cell.month = display_month_;
        cell.outside = false;
        if (day < 1) {
            cell.year = prev_year;
            cell.month = prev_month;
            day += prev_days;
            cell.outside = true;
        } else if (day > days) {
            day -= days;
            cell.month = display_month_ == 12 ? 1 : display_month_ + 1;
            cell.year = display_month_ == 12 ? display_year_ + 1 : display_year_;
            cell.outside = true;
        }
        cell.date = day;

        lv_obj_set_pos(cell.button, kGridX + column * kCellWidth + 1,
                       kGridY - kRodakosAppHeaderHeight + row * row_height + 1);
        lv_obj_set_size(cell.button, kCellWidth - 2, row_height - 2);

        const bool selected = !cell.outside && day == selected_day_;
        const bool today = cell.year == today_year && cell.month == today_month && day == today_day;
        lv_obj_set_style_bg_color(cell.button, rodakos_theme_primary(), 0);
        lv_obj_set_style_bg_opa(cell.button, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell.button, today && !selected && !cell.outside ? 2 : 0, 0);

        bool accent = false;
        const char* lunar = CellLunarText(cell.year, cell.month, day, accent, buffer, sizeof(buffer));
        lv_label_set_text_fmt(cell.day, "%d", day);
        lv_label_set_text(cell.lunar, lunar);

        lv_color_t day_color = column >= 5 ? rodakos_theme_warning() : rodakos_theme_text_primary();
        if (today && !selected) day_color = rodakos_theme_primary();
        lv_color_t lunar_color = accent ? rodakos_theme_secondary() : rodakos_theme_text_tertiary();
        if (selected) day_color = lunar_color = rodakos_theme_on_primary();
        lv_obj_set_style_text_color(cell.day, day_color, 0);
        lv_obj_set_style_text_color(cell.lunar, lunar_color, 0);
        const lv_opa_t opa = cell.outside ? LV_OPA_40 : LV_OPA_COVER;
        lv_obj_set_style_text_opa(cell.day, opa, 0);
        lv_obj_set_style_text_opa(cell.lunar, opa, 0);

        // Day number above, lunar line below, the pair centred in the cell.
        const lv_coord_t block = 14 + 1 + 13;
        const lv_coord_t top = (row_height - 2 - block) / 2;
        lv_obj_align(cell.day, LV_ALIGN_TOP_MID, 0, top - 2);
        lv_obj_align(cell.lunar, LV_ALIGN_TOP_MID, 0, top + 15);
    }
}

void CalendarApp::RenderDay() {
    if (day_view_ == nullptr) return;
    const int weekday = MondayWeekday(display_year_, display_month_, selected_day_);
    lv_label_set_text_fmt(title_label_, "%d 月 %d 日 %s", display_month_, selected_day_, kWeekdayLong[weekday]);
    lv_label_set_text_fmt(day_number_label_, "%d", selected_day_);

    std::tm now = {};
    int64_t diff = 0;
    if (ReadLocalTime(&now)) {
        diff = DayNumber(display_year_, display_month_, selected_day_) -
               DayNumber(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
    }
    if (diff == 0) {
        lv_label_set_text(day_relative_label_, "今天");
    } else if (diff == 1) {
        lv_label_set_text(day_relative_label_, "明天");
    } else if (diff == -1) {
        lv_label_set_text(day_relative_label_, "昨天");
    } else {
        lv_label_set_text_fmt(day_relative_label_, diff > 0 ? "%lld 天后" : "%lld 天前",
                              static_cast<long long>(diff > 0 ? diff : -diff));
    }
    lv_obj_set_style_text_color(day_number_label_, diff == 0 ? rodakos_theme_primary() : rodakos_theme_text_primary(), 0);

    rodakos::LunarDate lunar;
    const bool has_lunar = rodakos::SolarToLunar(display_year_, display_month_, selected_day_, lunar);
    size_t line = 0;
    if (has_lunar) {
        lv_label_set_text_fmt(day_lunar_label_, "农历%s%s月%s", lunar.leap ? "闰" : "",
                              rodakos::LunarMonthName(lunar.month), rodakos::LunarDayName(lunar.day));
        char year_name[24];
        rodakos::LunarYearName(lunar.year, year_name, sizeof(year_name));
        const char* special = rodakos::FestivalName(display_year_, display_month_, selected_day_, lunar);
        if (special == nullptr && lunar.solar_term >= 0) special = rodakos::SolarTermName(lunar.solar_term);
        if (special != nullptr) {
            lv_label_set_text_fmt(day_detail_labels_[line], "%s年 · %s", year_name, special);
            lv_obj_set_style_text_color(day_detail_labels_[line], rodakos_theme_secondary(), 0);
        } else {
            lv_label_set_text_fmt(day_detail_labels_[line], "%s年", year_name);
            lv_obj_set_style_text_color(day_detail_labels_[line], rodakos_theme_text_secondary(), 0);
        }
        ++line;
    } else {
        lv_label_set_text(day_lunar_label_, "");
    }

    const int64_t day_of_year = DayNumber(display_year_, display_month_, selected_day_) -
                                DayNumber(display_year_, 1, 1) + 1;
    lv_label_set_text_fmt(day_detail_labels_[line], "第 %d 周 · 第 %d 天",
                          IsoWeek(display_year_, display_month_, selected_day_), static_cast<int>(day_of_year));
    lv_obj_set_style_text_color(day_detail_labels_[line], rodakos_theme_text_secondary(), 0);
    ++line;

    int y = display_year_, m = display_month_, d = selected_day_;
    ShiftDate(y, m, d, 1);
    int days_until = 0;
    const int term = rodakos::NextSolarTerm(y, m, d, 31, days_until);
    if (term >= 0 && line < day_detail_labels_.size()) {
        lv_label_set_text_fmt(day_detail_labels_[line], "距%s %d 天", rodakos::SolarTermName(term), days_until + 1);
        lv_obj_set_style_text_color(day_detail_labels_[line], rodakos_theme_text_secondary(), 0);
        ++line;
    }
    for (; line < day_detail_labels_.size(); ++line) lv_label_set_text(day_detail_labels_[line], "");
}

void CalendarApp::RenderUnsynced() {
    if (unsynced_hint_ == nullptr) return;
    const bool syncing = sync_requested_ && TimeServiceGetSyncStatus() == TimeSyncStatus::kInProgress;
    lv_label_set_text(unsynced_hint_, syncing ? "正在从网络获取时间…" : "联网后自动校时，也可立即同步");
    lv_label_set_text(sync_button_label_, syncing ? "正在校时…" : "立即校时");
    if (syncing) {
        lv_obj_add_state(sync_button_, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(sync_button_, LV_STATE_DISABLED);
    }
}

void CalendarApp::RefreshToday() {
    std::tm now = {};
    if (!ReadLocalTime(&now)) {
        has_valid_clock_ = false;
        if (view_ != View::kUnsynced) ShowView(View::kUnsynced);
        RenderUnsynced();
        return;
    }
    if (!has_valid_clock_) {
        has_valid_clock_ = true;
        sync_requested_ = false;
        display_year_ = now.tm_year + 1900;
        display_month_ = now.tm_mon + 1;
        selected_day_ = now.tm_mday;
        if (refresh_timer_ != nullptr) lv_timer_set_period(refresh_timer_, kClockRefreshMs);
        ShowView(View::kMonth);
        return;
    }
    view_ == View::kDay ? RenderDay() : RenderMonth();
}

void CalendarApp::NavigateMonth(int delta) {
    if (!has_valid_clock_) return;
    display_month_ += delta;
    if (display_month_ < 1) {
        display_month_ = 12;
        --display_year_;
    } else if (display_month_ > 12) {
        display_month_ = 1;
        ++display_year_;
    }
    selected_day_ = std::min(selected_day_, DaysInMonth(display_year_, display_month_));
    if (view_ != View::kMonth) {
        ShowView(View::kMonth);
    } else {
        RenderMonth();
    }
}

void CalendarApp::NavigateDay(int delta) {
    if (!has_valid_clock_) return;
    ShiftDate(display_year_, display_month_, selected_day_, delta);
    RenderDay();
}

void CalendarApp::SelectToday() {
    std::tm now = {};
    if (!ReadLocalTime(&now)) return;
    display_year_ = now.tm_year + 1900;
    display_month_ = now.tm_mon + 1;
    selected_day_ = now.tm_mday;
    has_valid_clock_ = true;
    ShowView(View::kMonth);
}

void CalendarApp::OpenCell(size_t index) {
    if (index >= kDayCellCount || !has_valid_clock_) return;
    const Cell& cell = cells_[index];
    if (cell.outside) {
        // A greyed neighbour day moves the grid to its month and selects it there.
        selected_day_ = cell.date;
        display_year_ = cell.year;
        display_month_ = cell.month;
        RenderMonth();
        return;
    }
    selected_day_ = cell.date;
    ShowView(View::kDay);
}

void CalendarApp::StartSync() {
    if (!TimeServiceStartSavedSync()) {
        ui_->ShowToastUnlocked("无法开始校时，请检查网络");
        return;
    }
    sync_requested_ = true;
    if (refresh_timer_ != nullptr) {
        lv_timer_set_period(refresh_timer_, kSyncPollMs);
        lv_timer_reset(refresh_timer_);
    }
    RenderUnsynced();
}

void CalendarApp::NavigateBack() {
    if (view_ == View::kDay) {
        ShowView(View::kMonth);
        return;
    }
    NavigateHome();
}

void CalendarApp::NavigateHome() {
    if (context_ == nullptr) return;
    lv_async_call_cancel(DeferReturnHome, context_);
    lv_async_call(DeferReturnHome, context_);
}

void CalendarApp::HandleSwipe(lv_event_t* event) {
    const auto code = lv_event_get_code(event);
    auto* indev = lv_event_get_indev(event);
    if (code == LV_EVENT_PRESSED) {
        swipe_direction_ = 0;
        swipe_tracking_ = indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER;
        if (swipe_tracking_) lv_indev_get_point(indev, &press_origin_);
        return;
    }
    if (code == LV_EVENT_PRESS_LOST) {
        swipe_tracking_ = false;
        return;
    }
    if (!swipe_tracking_ || indev == nullptr) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    const int32_t dx = point.x - press_origin_.x;
    const int32_t dy = point.y - press_origin_.y;
    if (swipe_direction_ == 0 && std::abs(dx) >= kSwipeThreshold && 2 * std::abs(dx) >= 3 * std::abs(dy)) {
        swipe_direction_ = dx < 0 ? 1 : -1;
    }
    if (code == LV_EVENT_RELEASED) {
        swipe_tracking_ = false;
        if (swipe_direction_ != 0) {
            const int direction = swipe_direction_;
            // Cancel the cell click that would follow this release.
            lv_indev_reset(indev, nullptr);
            view_ == View::kDay ? NavigateDay(direction) : NavigateMonth(direction);
            swipe_direction_ = 0;
        }
    }
}

bool CalendarApp::ReadLocalTime(std::tm* result) const {
    int64_t unix_ms = 0;
    if (result == nullptr || !TimeServiceUnixTimeMs(unix_ms)) return false;
    const std::time_t now = static_cast<std::time_t>(unix_ms / 1000);
    return localtime_r(&now, result) != nullptr;
}

void RegisterCalendarApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "calendar",
        .title = "Calendar",
        .icon = ICON_CALENDAR,
        .category = PhoneAppCategory::kTools,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = true,
        .aliases = {"cal", "agenda", "日历", "日程", "农历"},
        .create = []() { return std::make_unique<CalendarApp>(); },
    });
}
