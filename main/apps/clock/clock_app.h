#pragma once

#include "phone_os/phone_app.h"

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

class ClockApp final : public PhoneApp {
public:
    ~ClockApp() override;

    const char* id() const override { return "clock"; }
    bool OnCreate(PhoneAppContext& context) override;
    void OnShow() override {}
    void OnHide() override {}
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    void UpdateClock();
    void UpdateSyncStatus();

private:
    void CreateUi();
    void DestroyUi();
    void ResetUiPointers();
    void StartSync();
    void NavigateHome();
    bool TimeIsValid() const;

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* time_row_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* seconds_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* timezone_label_ = nullptr;
    lv_obj_t* server_label_ = nullptr;
    lv_obj_t* sync_status_label_ = nullptr;
    lv_timer_t* clock_timer_ = nullptr;
    lv_timer_t* sync_timer_ = nullptr;

    bool sync_in_progress_ = false;
    uint32_t sync_poll_count_ = 0;
};

void RegisterClockApp(PhoneAppRegistry& registry);
