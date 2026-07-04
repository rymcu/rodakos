#pragma once

#include "phone_os/phone_app.h"

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class MotionService;
struct MotionSample;
}

class GyroApp final : public PhoneApp {
public:
    ~GyroApp() override;

    const char* id() const override { return "gyro"; }
    bool OnCreate(PhoneAppContext& context) override;
    void OnShow() override;
    void OnHide() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;

    void Refresh();

private:
    void CreateUi();
    void DestroyUi();
    void ResetUiPointers();
    lv_obj_t* CreateValuePanel(const char* title, lv_coord_t x, lv_coord_t y, uint32_t color);
    lv_obj_t* CreateAxisLabel(const char* text, lv_coord_t x, lv_coord_t y, uint32_t color);
    bool CreateAttitudeCanvas();
    void ReleaseAttitudeCanvas();
    void DrawAttitude(const rodakos::MotionSample& sample);
    void SetStatus(const char* text, bool ready);
    void NavigateHome();
    static void RefreshTimerCallback(lv_timer_t* timer);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::MotionService* motion_ = nullptr;
    bool motion_started_ = false;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* attitude_canvas_ = nullptr;
    lv_draw_buf_t attitude_draw_buf_ = {};
    void* attitude_canvas_buffer_ = nullptr;
    lv_obj_t* roll_value_label_ = nullptr;
    lv_obj_t* pitch_value_label_ = nullptr;
    lv_obj_t* yaw_value_label_ = nullptr;
    lv_obj_t* gyro_x_label_ = nullptr;
    lv_obj_t* gyro_y_label_ = nullptr;
    lv_obj_t* gyro_z_label_ = nullptr;
    lv_obj_t* acc_x_label_ = nullptr;
    lv_obj_t* acc_y_label_ = nullptr;
    lv_obj_t* acc_z_label_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
};

void RegisterGyroApp(PhoneAppRegistry& registry);
