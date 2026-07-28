#pragma once

#include "phone_os/phone_app.h"

#include <array>
#include <cstddef>
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

private:
    static constexpr size_t kDayCellCount = 42;

    void CreateUi();
    void DestroyUi();
    void ResetUiPointers();
    void RenderMonth();
    void RenderSelection();
    void RefreshToday();
    void NavigateMonth(int delta);
    void SelectToday();
    void SelectDay(lv_obj_t* target);
    void NavigateHome();
    bool ReadLocalTime(std::tm* result) const;
    static void RefreshTimerCallback(lv_timer_t* timer);
    static void DayButtonCallback(lv_event_t* event);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* month_label_ = nullptr;
    lv_obj_t* selected_label_ = nullptr;
    lv_obj_t* sync_status_label_ = nullptr;
    lv_obj_t* today_button_ = nullptr;
    lv_obj_t* previous_button_ = nullptr;
    lv_obj_t* next_button_ = nullptr;
    std::array<lv_obj_t*, kDayCellCount> day_buttons_{};
    std::array<lv_obj_t*, kDayCellCount> day_labels_{};
    lv_timer_t* refresh_timer_ = nullptr;

    int display_year_ = 1970;
    int display_month_ = 1;
    int selected_day_ = 1;
    bool has_valid_clock_ = false;
    bool paused_ = false;
};

void RegisterCalendarApp(PhoneAppRegistry& registry);
