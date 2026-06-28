#pragma once

#include "phone_os/phone_app.h"
#include "rodakos_adapters/file_service.h"

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

class SystemInfoApp final : public PhoneApp {
public:
    ~SystemInfoApp() override;

    const char* id() const override { return "system"; }
    bool OnCreate(PhoneAppContext& context) override;
    void OnShow() override {}
    void OnHide() override {}
    void OnDestroy() override;
    void OnTick() override {}

    void Refresh();

private:
    struct InfoLabels {
        lv_obj_t* value = nullptr;
        lv_obj_t* detail = nullptr;
    };

    void CreateUi();
    InfoLabels CreateInfoCard(lv_obj_t* parent, const char* icon, const char* title);
    void ProbeStorage(bool allow_mount);
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;

    InfoLabels wifi_;
    InfoLabels memory_;
    InfoLabels storage_;
    InfoLabels uptime_;
    InfoLabels firmware_;
    InfoLabels chip_;
    InfoLabels heap_detail_;

    bool storage_checked_ = false;
    bool storage_mounted_ = false;
    rodakos::FileService::Capacity storage_capacity_;
};

void RegisterSystemInfoApp(PhoneAppRegistry& registry);
