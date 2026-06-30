#pragma once

#include "phone_os/phone_app.h"

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class VoiceAssistantService;
class VoiceWakeService;
}

class AssistantApp final : public PhoneApp {
public:
    ~AssistantApp() override;

    const char* id() const override { return "assistant"; }
    bool OnCreate(PhoneAppContext& context) override;
    void OnShow() override;
    void OnHide() override;
    void OnDestroy() override;
    void OnTick() override {}

    void RefreshState();

private:
    void CreateUi();
    void ToggleWakeListening(bool enabled);
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::VoiceAssistantService* assistant_ = nullptr;
    rodakos::VoiceWakeService* wake_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    lv_obj_t* wake_switch_ = nullptr;
    lv_obj_t* wake_status_label_ = nullptr;
    lv_obj_t* assistant_status_label_ = nullptr;
    lv_obj_t* assistant_detail_label_ = nullptr;
    lv_obj_t* runtime_detail_label_ = nullptr;
    lv_obj_t* cloud_detail_label_ = nullptr;
};

void RegisterAssistantApp(PhoneAppRegistry& registry);
