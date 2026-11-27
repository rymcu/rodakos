#pragma once

#include "phone_os/motion_service.h"
#include "phone_os/phone_app.h"

#include <array>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

// Bubble level and attitude readout. The main page is a level on the left and roll / pitch /
// yaw on the right; the six-axis page shows raw angular rate and acceleration as centred bars.
class GyroApp final : public PhoneApp {
public:
    static constexpr lv_coord_t kCanvasSize = 154;
    // Both roll and pitch within this many degrees reads as level.
    static constexpr float kLevelToleranceDeg = 1.5f;

    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    bool OnBackRequested() override;

    void Refresh();

private:
    enum class View { kLevel, kAxes, kError };

    struct AxisBar {
        lv_obj_t* fill = nullptr;
        lv_obj_t* value = nullptr;
    };

    void CreateUi();
    void DestroyUi();
    void CreateLevelView();
    void CreateAxesView();
    void CreateErrorView();
    void ShowView(View view);
    bool CreateAttitudeCanvas(lv_obj_t* parent);
    void ReleaseAttitudeCanvas();
    void DrawAttitude(float roll, float pitch, float yaw, bool level);
    void UpdateAxisBar(AxisBar& bar, float value, float full_scale, const char* format);
    void ToggleZero();
    void ToggleFreeze();
    void Retry();
    void NavigateHome();
    void UpdateButtons();
    static void RefreshTimerCallback(lv_timer_t* timer);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::MotionService* motion_ = nullptr;
    bool motion_started_ = false;

    View view_ = View::kLevel;
    // The view to return to from the error page once the sensor reads again.
    View data_view_ = View::kLevel;
    bool zeroed_ = false;
    float zero_roll_ = 0.0f;
    float zero_pitch_ = 0.0f;
    float zero_yaw_ = 0.0f;
    bool frozen_ = false;
    rodakos::MotionSample last_sample_{};
    bool has_sample_ = false;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* action_button_ = nullptr;
    lv_obj_t* action_icon_ = nullptr;

    lv_obj_t* level_view_ = nullptr;
    lv_obj_t* attitude_canvas_ = nullptr;
    lv_draw_buf_t attitude_draw_buf_ = {};
    void* attitude_canvas_buffer_ = nullptr;
    lv_obj_t* state_dot_ = nullptr;
    lv_obj_t* state_label_ = nullptr;
    lv_obj_t* relative_tag_ = nullptr;
    std::array<lv_obj_t*, 3> angle_values_{};
    lv_obj_t* zero_button_ = nullptr;
    lv_obj_t* zero_label_ = nullptr;
    lv_obj_t* freeze_button_ = nullptr;
    lv_obj_t* freeze_label_ = nullptr;
    lv_obj_t* summary_label_ = nullptr;

    lv_obj_t* axes_view_ = nullptr;
    lv_obj_t* axes_info_label_ = nullptr;
    std::array<AxisBar, 3> gyro_bars_{};
    std::array<AxisBar, 3> accel_bars_{};

    lv_obj_t* error_view_ = nullptr;
    lv_obj_t* error_hint_label_ = nullptr;

    lv_timer_t* refresh_timer_ = nullptr;
};

void RegisterGyroApp(PhoneAppRegistry& registry);
