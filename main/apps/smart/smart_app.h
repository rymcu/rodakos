#pragma once

#include "phone_os/phone_app.h"
#include "phone_os/light_service.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

class SmartApp final : public PhoneApp {
public:
    static constexpr size_t kPresetCount = 6;
    static constexpr size_t kMaxTiles = 4;

    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    bool OnBackRequested() override;

    void Refresh();

private:
    enum class View { kControl, kOverview, kEmpty };

    struct Tile {
        lv_obj_t* card = nullptr;
        lv_obj_t* orb = nullptr;
        lv_obj_t* orb_icon = nullptr;
        lv_obj_t* power = nullptr;
        lv_obj_t* name = nullptr;
        lv_obj_t* dot = nullptr;
        lv_obj_t* status = nullptr;
    };

    static void RefreshTimerCallback(lv_timer_t* timer);

    void CreateUi();
    void DestroyUi();
    void CreateControlView();
    void CreateOverviewView();
    void CreateEmptyView();
    void ShowView(View view);
    void RefreshControl();
    void RefreshOverview();

    void OpenLight(size_t index);
    void TogglePower();
    void ToggleTile(size_t index);
    void PreviewBrightness(int32_t value);
    void CommitBrightness();
    void SelectPreset(size_t preset);
    void Apply(const std::string& id, const rodakos::LightPatch& patch);
    bool CurrentLight(rodakos::LightState& state) const;
    void NavigateBack();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::LightService* lights_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    View view_ = View::kControl;
    size_t current_index_ = 0;
    bool paused_ = false;
    // Light id captured when a brightness drag starts, so a list change mid-drag cannot
    // redirect the commit to another light.
    std::string drag_light_id_;
    bool dragging_ = false;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;

    lv_obj_t* control_ = nullptr;
    lv_obj_t* hero_ = nullptr;
    lv_obj_t* orb_ = nullptr;
    lv_obj_t* orb_icon_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* status_dot_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* power_switch_ = nullptr;
    lv_obj_t* brightness_icon_ = nullptr;
    lv_obj_t* brightness_value_ = nullptr;
    lv_obj_t* brightness_slider_ = nullptr;
    lv_obj_t* color_name_label_ = nullptr;
    std::array<lv_obj_t*, kPresetCount> presets_{};
    std::array<lv_obj_t*, kPresetCount> preset_checks_{};

    lv_obj_t* overview_ = nullptr;
    std::array<Tile, kMaxTiles> tiles_{};

    lv_obj_t* empty_ = nullptr;
    lv_obj_t* empty_hint_ = nullptr;
};

void RegisterSmartApp(PhoneAppRegistry& registry);
