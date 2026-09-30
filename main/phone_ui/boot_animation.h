#pragma once

#include <cstdint>

#include <lvgl.h>

class PhoneUi;

class BootAnimation {
public:
    explicit BootAnimation(PhoneUi& ui);
    ~BootAnimation();

    BootAnimation(const BootAnimation&) = delete;
    BootAnimation& operator=(const BootAnimation&) = delete;

    bool Start();
    void Finish();
    void Stop();

private:
    static constexpr uint32_t kFramePeriodMs = 33;
    static constexpr uint32_t kMinimumDisplayMs = 650;
    static constexpr uint32_t kFadeOutMs = 220;

    static void TimerCallback(lv_timer_t* timer);
    void Tick();
    void DestroyUnlocked();

    PhoneUi& ui_;
    lv_obj_t* root_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    uint32_t elapsed_ms_ = 0;
    uint32_t finish_elapsed_ms_ = 0;
    bool finish_requested_ = false;
    bool finishing_ = false;
};
