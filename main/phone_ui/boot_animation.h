#pragma once

#include <cstddef>
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
    static constexpr uint32_t kMinimumDisplayMs = 2280;
    static constexpr uint32_t kFadeOutMs = 220;
    static constexpr uint32_t kLogoStartMs = 80;
    static constexpr uint32_t kLetterStaggerMs = 70;
    static constexpr uint32_t kLetterDurationMs = 260;
    static constexpr size_t kLogoLetterCount = 7;
    static constexpr int32_t kLogoLetterCellWidth = 41;
    static constexpr int32_t kLogoBaseY = 105;
    static constexpr int32_t kLogoOffsetY = 10;

    static void TimerCallback(lv_timer_t* timer);
    void Tick();
    void DestroyUnlocked();
    void UpdateIntroUnlocked();

    PhoneUi& ui_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* logo_letters_[kLogoLetterCount] = {};
    lv_timer_t* timer_ = nullptr;
    uint32_t elapsed_ms_ = 0;
    uint32_t finish_elapsed_ms_ = 0;
    bool finish_requested_ = false;
    bool finishing_ = false;
};
