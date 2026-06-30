#pragma once

#include "phone_os/phone_app.h"
#include "phone_os/light_service.h"

#include <cstddef>
#include <vector>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

class SmartApp final : public PhoneApp {
public:
    ~SmartApp() override;

    const char* id() const override { return "smart"; }
    bool OnCreate(PhoneAppContext& context) override;
    void OnShow() override;
    void OnHide() override;
    void OnDestroy() override;
    void OnTick() override {}

    void Refresh();

private:
    void CreateUi();
    void RebuildLightList();
    void CreateLightButton(lv_obj_t* parent, size_t light_index);
    void CreatePresetButton(lv_obj_t* parent,
                            const char* label,
                            size_t preset_index,
                            lv_coord_t x,
                            lv_coord_t y);
    void SelectLight(size_t index);
    const rodakos::LightState* SelectedLight() const;
    bool HasSelectedLight() const;
    void SetControlsDisabled(bool disabled);
    void SetPower(bool enabled);
    void SetBrightness(uint8_t brightness);
    void SetColor(rodakos::RgbColor color);
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::LightService* lights_ = nullptr;
    size_t selected_index_ = 0;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* light_list_ = nullptr;
    lv_obj_t* empty_label_ = nullptr;
    lv_obj_t* light_title_label_ = nullptr;
    lv_obj_t* power_switch_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* color_preview_ = nullptr;
    lv_obj_t* brightness_slider_ = nullptr;
    lv_obj_t* brightness_label_ = nullptr;
    std::vector<lv_obj_t*> light_buttons_;
    std::vector<lv_obj_t*> preset_buttons_;
};

void RegisterSmartApp(PhoneAppRegistry& registry);
