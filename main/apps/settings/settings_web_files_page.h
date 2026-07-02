#pragma once

#include <lvgl.h>

class PhoneAppContext;
class PhoneUi;

class SettingsWebFilesPage {
public:
    void Create(lv_obj_t* reference_body, PhoneAppContext& context, PhoneUi& ui);
    void Hide();
    void Show();
    void Update();
    void Reset();

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
};
