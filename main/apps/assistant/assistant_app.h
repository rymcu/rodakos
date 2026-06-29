#pragma once

#include "phone_os/phone_app.h"

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class VoiceAssistantService;
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
    void StartInteraction();
    void StopInteraction();
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::VoiceAssistantService* assistant_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    lv_obj_t* phase_label_ = nullptr;
    lv_obj_t* detail_label_ = nullptr;
    lv_obj_t* focus_label_ = nullptr;
    lv_obj_t* cloud_label_ = nullptr;
    lv_obj_t* cloud_detail_label_ = nullptr;
};

void RegisterAssistantApp(PhoneAppRegistry& registry);
