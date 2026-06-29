#pragma once

#include "phone_os/phone_app.h"

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

class HomeApp final : public PhoneApp {
public:
    ~HomeApp() override;

    const char* id() const override { return "home"; }
    bool OnCreate(PhoneAppContext& context) override;
    void OnShow() override {}
    void OnHide() override {}
    void OnDestroy() override;
    void UpdateClock();

private:
    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* grid_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    lv_obj_t* status_cluster_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* wifi_label_ = nullptr;
    lv_timer_t* clock_timer_ = nullptr;
};

void RegisterHomeApp(PhoneAppRegistry& registry);
