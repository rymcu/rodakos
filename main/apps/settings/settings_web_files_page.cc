#include "apps/settings/settings_web_files_page.h"

#include "apps/settings/settings_app_internal.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_os/web_file_system_service.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <algorithm>
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

    // Status card: state, address and access code on separate static lines (easier to copy than
    // a scrolling URL), then the upload in progress or the last upload.
    auto* card = CreateGroupList(body_, kGridX, 4, kGridWidth, 138);
    lv_obj_set_style_pad_hor(card, 12, 0);
    lv_obj_set_style_pad_ver(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    status_label_ = CreateSettingLabel(card, "", false);
    lv_obj_set_width(status_label_, 276);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(status_label_, LV_ALIGN_TOP_LEFT, 0, 0);

    // Two numbered steps: the code is typed into the browser page, not into the address.
    step_label_ = CreateSettingLabel(card, "1. 电脑浏览器打开", true);
    lv_obj_set_style_text_font(step_label_, &phone_font_12, 0);
    lv_obj_align(step_label_, LV_ALIGN_TOP_LEFT, 0, 20);

    url_label_ = CreateSettingLabel(card, "", false);
    lv_obj_set_width(url_label_, 276);
    lv_label_set_long_mode(url_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(url_label_, &phone_font_14, 0);
    lv_obj_align(url_label_, LV_ALIGN_TOP_LEFT, 0, 36);

    token_caption_ = CreateSettingLabel(card, "2. 输入访问码", true);
    lv_obj_set_style_text_font(token_caption_, &phone_font_12, 0);
    lv_obj_align(token_caption_, LV_ALIGN_TOP_LEFT, 0, 56);

    // 8 characters fit in the 26 px digit font; easier to read across a desk.
    token_label_ = lv_label_create(card);
    lv_obj_set_style_text_font(token_label_, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(token_label_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_letter_space(token_label_, 3, 0);
    lv_obj_align(token_label_, LV_ALIGN_TOP_LEFT, 0, 70);

    last_label_ = CreateSettingLabel(card, "", true);
    lv_obj_set_width(last_label_, 276);
    lv_label_set_long_mode(last_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(last_label_, &phone_font_12, 0);
    lv_obj_align(last_label_, LV_ALIGN_TOP_LEFT, 0, 102);

    progress_bar_ = lv_bar_create(card);
    lv_obj_set_size(progress_bar_, 276, 6);
    lv_obj_align(progress_bar_, LV_ALIGN_TOP_LEFT, 0, 117);
    lv_bar_set_range(progress_bar_, 0, 100);
    lv_obj_set_style_bg_color(progress_bar_, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_add_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);

    // One button whose action follows the state: 开启 when stopped, 关闭 when running.
    start_btn_ = CreateSettingButton(body_, "", SettingButtonStyle::kPrimary, kGridWidth, kActionHeight,
                                     &toggle_label_);
    lv_obj_set_pos(start_btn_, kGridX, kActionY);
    lv_obj_add_event_cb(start_btn_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsWebFilesPage*>(lv_event_get_user_data(e));
        if (self == nullptr || self->context_ == nullptr) return;
        auto* web_files = self->context_->services().web_files();
        if (web_files != nullptr && web_files->IsRunning()) self->Stop();
        else self->Start();
    }, LV_EVENT_CLICKED, this);
    stop_btn_ = nullptr;

    // Upload progress changes without user input; poll while the page exists.
    refresh_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<SettingsWebFilesPage*>(lv_timer_get_user_data(timer));
        if (self != nullptr && self->body_ != nullptr &&
            !lv_obj_has_flag(self->body_, LV_OBJ_FLAG_HIDDEN)) {
            self->Update();
        }
    }, 1000, this);

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

namespace {
std::string FormatSize(size_t bytes) {
    char text[24];
    if (bytes >= 1024 * 1024) {
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(text, sizeof(text), "%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
    }
    return text;
}

std::string BaseName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

void SettingsWebFilesPage::Update() {
    auto* web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
    if (web_files == nullptr || status_label_ == nullptr) {
        return;
    }

    const auto state = web_files->GetState();
    const bool stopping = stop_guard_ && stop_guard_->stopping.load();
    const char* status = stopping ? "正在关闭…"
        : !state.running ? "已关闭"
        : state.busy ? "运行中 · 正在传输"
        : "运行中";
    lv_label_set_text(status_label_, status);
    lv_obj_set_style_text_color(status_label_,
                                state.running && !stopping ? rodakos_theme_success()
                                                           : rodakos_theme_text_primary(),
                                0);

    if (state.running) {
        lv_label_set_text(url_label_, state.url.c_str());
        // The service already formats the code as "K7QM 4RX8".
        lv_label_set_text(token_label_, state.token.c_str());
        lv_obj_clear_flag(step_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(token_caption_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(token_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(url_label_, "开启后，同一网络的电脑可以用浏览器读写 SD 卡。");
        lv_obj_add_flag(step_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(token_caption_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(token_label_, LV_OBJ_FLAG_HIDDEN);
    }

    if (state.busy && state.active_total_bytes > 0) {
        const int percent = static_cast<int>(
            std::min<size_t>(100, state.active_bytes * 100 / state.active_total_bytes));
        const std::string text = "上传 " + BaseName(state.last_file) + "  " + std::to_string(percent) + "%";
        lv_label_set_text(last_label_, text.c_str());
        lv_bar_set_value(progress_bar_, percent, LV_ANIM_OFF);
        lv_obj_clear_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(progress_bar_, LV_OBJ_FLAG_HIDDEN);
        if (state.last_file.empty()) {
            lv_label_set_text(last_label_, "还没有上传过文件");
        } else {
            const std::string text = "最近上传：" + BaseName(state.last_file) + "（" +
                                     FormatSize(state.last_bytes) + "）";
            lv_label_set_text(last_label_, text.c_str());
        }
    }

    if (start_btn_ != nullptr) {
        const bool running = state.running && !stopping;
        lv_label_set_text(toggle_label_, stopping ? "正在关闭…" : running ? "关闭" : "开启");
        StyleSettingButton(start_btn_, toggle_label_,
                           running ? SettingButtonStyle::kSecondary : SettingButtonStyle::kPrimary);
        if (stopping) lv_obj_add_state(start_btn_, LV_STATE_DISABLED);
        else lv_obj_clear_state(start_btn_, LV_STATE_DISABLED);
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
    toggle_label_ = nullptr;
    token_caption_ = nullptr;
    step_label_ = nullptr;
    token_label_ = nullptr;
    progress_bar_ = nullptr;
    // LVGL timers are not owned by objects; deleting the page widgets does not stop this one.
    if (refresh_timer_ != nullptr) lv_timer_delete(refresh_timer_);
    refresh_timer_ = nullptr;
}

void SettingsWebFilesPage::Start() {
    auto* wifi = context_ != nullptr ? context_->services().wifi() : nullptr;
    auto* web_files = context_ != nullptr ? context_->services().web_files() : nullptr;
    if (wifi == nullptr || web_files == nullptr) {
        ui_->ShowToastUnlocked("网页传文件不可用");
        return;
    }
    if (wifi->GetStatus() != WiFiStatus::kConnected || wifi->GetIPAddress().empty()) {
        if (status_label_ != nullptr) {
            lv_label_set_text(status_label_, "请先连接 Wi-Fi");
            lv_obj_set_style_text_color(status_label_, rodakos_theme_warning(), 0);
        }
        return;
    }

    if (!web_files->Start(wifi->GetIPAddress())) {
        Update();
        if (status_label_ != nullptr) {
            lv_label_set_text(status_label_, "开启失败，请稍后重试");
            lv_obj_set_style_text_color(status_label_, rodakos_theme_error(), 0);
        }
        return;
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
        ui_->ShowToastUnlocked("无法关闭，请稍后重试");
        return;
    }
    Update();
}
