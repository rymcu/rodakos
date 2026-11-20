#pragma once

#include <lvgl.h>

#include <atomic>
#include <memory>

class PhoneAppContext;
class PhoneUi;

class SettingsWebFilesPage {
public:
    void Create(lv_obj_t* reference_body, PhoneAppContext& context, PhoneUi& ui);
    void Hide();
    void Show();
    void Update();
    void Reset();

    // Shared with the stop worker so a late completion never touches a destroyed page.
    struct StopGuard {
        std::atomic<SettingsWebFilesPage*> page{nullptr};
        std::atomic<bool> stopping{false};
    };

private:
    void Start();
    void Stop();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* url_label_ = nullptr;
    lv_obj_t* last_label_ = nullptr;
    lv_obj_t* start_btn_ = nullptr;
    lv_obj_t* stop_btn_ = nullptr;
    lv_obj_t* toggle_label_ = nullptr;
    lv_obj_t* token_caption_ = nullptr;
    lv_obj_t* step_label_ = nullptr;
    lv_obj_t* token_label_ = nullptr;
    lv_obj_t* progress_bar_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    std::shared_ptr<StopGuard> stop_guard_;
};
