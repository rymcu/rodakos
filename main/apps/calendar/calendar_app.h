#pragma once

#include "phone_os/phone_app.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

class CalendarApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    bool OnBackRequested() override;

private:
    static constexpr size_t kDayCellCount = 42;

    enum class View { kMonth, kDay, kUnsynced };

    struct Cell {
        lv_obj_t* button = nullptr;
        lv_obj_t* day = nullptr;
        lv_obj_t* lunar = nullptr;
        // Solar date this cell shows; days of the neighbouring months switch month when tapped.
        int year = 0;
        int month = 0;
        int date = 0;
        bool outside = false;
    };

    void CreateUi();
    void DestroyUi();
    void CreateMonthView();
    void CreateDayView();
    void CreateUnsyncedView();
    void ShowView(View view);
    void RenderMonth();
    void RenderDay();
    void RenderUnsynced();
    void RefreshToday();
    void NavigateMonth(int delta);
    void NavigateDay(int delta);
    void SelectToday();
    void OpenCell(size_t index);
    void StartSync();
    void NavigateBack();
    void NavigateHome();
    bool ReadLocalTime(std::tm* result) const;
    void HandleSwipe(lv_event_t* event);
    static void RefreshTimerCallback(lv_timer_t* timer);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* header_ = nullptr;
    lv_obj_t* back_button_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* previous_button_ = nullptr;
    lv_obj_t* next_button_ = nullptr;
    lv_obj_t* today_button_ = nullptr;
    lv_obj_t* today_label_ = nullptr;

    lv_obj_t* month_view_ = nullptr;
    std::array<lv_obj_t*, 7> weekday_labels_{};
    std::array<Cell, kDayCellCount> cells_{};

    lv_obj_t* day_view_ = nullptr;
    lv_obj_t* day_number_label_ = nullptr;
    lv_obj_t* day_relative_label_ = nullptr;
    lv_obj_t* day_lunar_label_ = nullptr;
    std::array<lv_obj_t*, 3> day_detail_labels_{};

    lv_obj_t* unsynced_view_ = nullptr;
    lv_obj_t* unsynced_hint_ = nullptr;
    lv_obj_t* sync_button_ = nullptr;
    lv_obj_t* sync_button_label_ = nullptr;

    lv_timer_t* refresh_timer_ = nullptr;

    View view_ = View::kMonth;
    int display_year_ = 1970;
    int display_month_ = 1;
    int selected_day_ = 1;
    bool has_valid_clock_ = false;
    bool sync_requested_ = false;
    bool paused_ = false;
    lv_point_t press_origin_{};
    int swipe_direction_ = 0;
    bool swipe_tracking_ = false;
};

void RegisterCalendarApp(PhoneAppRegistry& registry);
