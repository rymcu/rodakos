#pragma once

#include "phone_ui/phone_ui.h"

#include <functional>
#include <string>

class PhoneScreen {
public:
    struct Options {
        std::string title;
        std::string subtitle;
        std::string icon;
        bool show_back = false;
        std::function<void()> on_back;
    };

    PhoneScreen(PhoneUi& ui, Options options);
    ~PhoneScreen();

    lv_obj_t* root() const { return root_; }
    lv_obj_t* content() const { return content_; }

private:
    PhoneUi& ui_;
    std::function<void()> back_callback_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* content_ = nullptr;
};
