#include "apps/settings/settings_web_files_page.h"

#include "apps/settings/settings_app_internal.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_os/web_file_system_service.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <cstdio>
#include <string>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

using namespace rodakos_settings;

void SettingsWebFilesPage::Create(lv_obj_t* reference_body,
                                  PhoneAppContext& context,
                                  PhoneUi& ui) {
    context_ = &context;
    ui_ = &ui;
    if (!stop_guard_) {
        stop_guard_ = std::make_shared<StopGuard>();
        stop_guard_->page.store(this);
    }
    if (body_ != nullptr) {
        return;
    }

    body_ = lv_obj_create(lv_obj_get_parent(reference_body));
    lv_obj_remove_style_all(body_);
    lv_obj_set_size(body_, lv_obj_get_width(reference_body), lv_obj_get_height(reference_body));
    lv_obj_set_pos(body_, lv_obj_get_x(reference_body), lv_obj_get_y(reference_body));
    lv_obj_set_style_bg_opa(body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(body_, 0, 0);
    lv_obj_add_flag(body_, LV_OBJ_FLAG_HIDDEN);

    auto* status_card = CreateSettingCard(body_, 4, 72);
    lv_obj_set_style_pad_all(status_card, 10, 0);

    auto* status_icon = lv_label_create(status_card);
    lv_label_set_text(status_icon, ICON_CLOUD);
    lv_obj_set_style_text_color(status_icon, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(status_icon, PhoneIconFont(), 0);
    lv_obj_align(status_icon, LV_ALIGN_LEFT_MID, 0, 0);

    auto* status_title = CreateSettingLabel(status_card, "File service", true);
    lv_obj_set_style_text_font(status_title, &phone_font_12, 0);
    lv_obj_align(status_title, LV_ALIGN_TOP_LEFT, 32, 0);

    status_label_ = CreateSettingLabel(status_card, "Stopped", false);
    lv_obj_set_width(status_label_, 240);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(status_label_, LV_ALIGN_TOP_LEFT, 32, 22);

    url_label_ = CreateSettingLabel(status_card, "Start to show URL", true);
    lv_obj_set_width(url_label_, 240);
    lv_label_set_long_mode(url_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(url_label_, LV_ALIGN_TOP_LEFT, 32, 42);

    auto* last_card = CreateSettingCard(body_, 84, 50);
    lv_obj_set_style_pad_all(last_card, 10, 0);

    auto* last_title = CreateSettingLabel(last_card, "Last upload", true);
    lv_obj_set_style_text_font(last_title, &phone_font_12, 0);
    lv_obj_align(last_title, LV_ALIGN_TOP_LEFT, 0, 0);

    last_label_ = CreateSettingLabel(last_card, "None", false);
    lv_obj_set_width(last_label_, 276);
    lv_label_set_long_mode(last_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(last_label_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    auto* controls = lv_obj_create(body_);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, 300, 38);
    lv_obj_set_pos(controls, 10, 148);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    start_btn_ = lv_btn_create(controls);
    lv_obj_set_size(start_btn_, 142, 34);
    lv_obj_set_style_bg_color(start_btn_, rodakos_theme_primary(), 0);
    lv_obj_set_style_radius(start_btn_, 6, 0);
    lv_obj_set_style_shadow_width(start_btn_, 0, 0);
    auto* start_label = lv_label_create(start_btn_);
    lv_label_set_text(start_label, "Start");
    lv_obj_set_style_text_color(start_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(start_label, &phone_font_12, 0);
    lv_obj_center(start_label);
    lv_obj_add_event_cb(start_btn_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsWebFilesPage*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->Start();
        }
    }, LV_EVENT_CLICKED, this);

    stop_btn_ = lv_btn_create(controls);
    lv_obj_set_size(stop_btn_, 142, 34);
    lv_obj_set_style_bg_color(stop_btn_, rodakos_theme_error(), 0);
    lv_obj_set_style_radius(stop_btn_, 6, 0);
    lv_obj_set_style_shadow_width(stop_btn_, 0, 0);
    auto* stop_label = lv_label_create(stop_btn_);
    lv_label_set_text(stop_label, "Stop");
    lv_obj_set_style_text_color(stop_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(stop_label, &phone_font_12, 0);
    lv_obj_center(stop_label);
    lv_obj_add_event_cb(stop_btn_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsWebFilesPage*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->Stop();
        }
    }, LV_EVENT_CLICKED, this);

    Update();
}

void SettingsWebFilesPage::Hide() {
    if (body_ != nullptr) {
        lv_obj_add_flag(body_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsWebFilesPage::Show() {
    if (body_ != nullptr) {
        lv_obj_clear_flag(body_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsWebFilesPage::Update() {
    auto* web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
    if (web_files == nullptr || status_label_ == nullptr) {
        return;
    }

    const auto state = web_files->GetState();
    const bool stopping = stop_guard_ && stop_guard_->stopping.load();
    const std::string status = stopping ? "正在停止…"
        : state.running ? (state.busy ? "Uploading..." : "Running")
        : "Stopped";
    lv_label_set_text(status_label_, status.c_str());
    lv_label_set_text(url_label_, state.running ? state.url.c_str() : state.message.c_str());

    if (state.last_file.empty()) {
        lv_label_set_text(last_label_, "None");
    } else {
        char text[220] = {};
        std::snprintf(text,
                      sizeof(text),
                      "%s (%u KB)",
                      state.last_file.c_str(),
                      static_cast<unsigned>((state.last_bytes + 1023) / 1024));
        lv_label_set_text(last_label_, text);
    }

    if (start_btn_ != nullptr) {
        if (state.running || stopping) {
            lv_obj_add_state(start_btn_, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(start_btn_, LV_STATE_DISABLED);
        }
    }
    if (stop_btn_ != nullptr) {
        if (state.running && !stopping) {
            lv_obj_clear_state(stop_btn_, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(stop_btn_, LV_STATE_DISABLED);
        }
    }
}

void SettingsWebFilesPage::Reset() {
    if (stop_guard_) stop_guard_->page.store(nullptr);
    stop_guard_.reset();
    context_ = nullptr;
    ui_ = nullptr;
    body_ = nullptr;
    status_label_ = nullptr;
    url_label_ = nullptr;
    last_label_ = nullptr;
    start_btn_ = nullptr;
    stop_btn_ = nullptr;
}

void SettingsWebFilesPage::Start() {
    auto* wifi = context_ != nullptr ? context_->services().wifi() : nullptr;
    auto* web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
    if (wifi == nullptr || web_files == nullptr) {
        ui_->ShowToastUnlocked("Web files unavailable");
        return;
    }
    if (wifi->GetStatus() != WiFiStatus::kConnected || wifi->GetIPAddress().empty()) {
        ui_->ShowToastUnlocked("Connect WiFi first");
        if (status_label_ != nullptr) {
            lv_label_set_text(status_label_, "WiFi not connected");
        }
        return;
    }

    if (web_files->Start(wifi->GetIPAddress())) {
        const auto state = web_files->GetState();
        ui_->ShowToastUnlocked("Web files started");
        if (url_label_ != nullptr) {
            lv_label_set_text(url_label_, state.url.c_str());
        }
    } else {
        const auto state = web_files->GetState();
        ui_->ShowToastUnlocked("Web files failed");
        if (status_label_ != nullptr) {
            lv_label_set_text(status_label_, state.message.c_str());
        }
    }
    Update();
}

namespace {
struct WebFilesStopArgs {
    std::shared_ptr<SettingsWebFilesPage::StopGuard> guard;
    rodakos::WebFileSystemService* web_files = nullptr;
};

void WebFilesStopDelivered(void* user_data) {
    auto* args = static_cast<WebFilesStopArgs*>(user_data);
    args->guard->stopping.store(false);
    if (auto* page = args->guard->page.load()) page->Update();
    delete args;
}

// httpd_stop waits for in-flight handlers, so an upload in progress would freeze the LVGL thread.
void WebFilesStopTask(void* arg) {
    auto* args = static_cast<WebFilesStopArgs*>(arg);
    args->web_files->Stop();
    bool queued = false;
    if (lvgl_port_lock(1000)) {
        queued = lv_async_call(WebFilesStopDelivered, args) == LV_RESULT_OK;
        lvgl_port_unlock();
    }
    if (!queued) {
        args->guard->stopping.store(false);
        delete args;
    }
    vTaskDelete(nullptr);
}
}  // namespace

void SettingsWebFilesPage::Stop() {
    auto* web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
    if (web_files == nullptr || !stop_guard_) {
        return;
    }
    bool expected = false;
    if (!stop_guard_->stopping.compare_exchange_strong(expected, true)) return;
    auto* args = new (std::nothrow) WebFilesStopArgs{stop_guard_, web_files};
    if (args == nullptr ||
        xTaskCreate(WebFilesStopTask, "web_stop", 4096, args, 3, nullptr) != pdPASS) {
        delete args;
        stop_guard_->stopping.store(false);
        ui_->ShowToastUnlocked("无法停止，请稍后重试");
        return;
    }
    Update();
}
