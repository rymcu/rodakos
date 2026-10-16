#include "apps/themes/themes_app.h"

#include "phone_os/appearance_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>

#include <cstdio>
#include <memory>

namespace {
constexpr const char* TAG = "ThemesApp";
constexpr uint32_t kStatusRefreshMs = 750;
constexpr lv_coord_t kPreviewWidth = 112;
constexpr lv_coord_t kPreviewHeight = 84;

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().ReturnHome();
    }
}

void DeferRefreshTheme(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().RefreshTheme();
    }
}

void StatusTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<ThemesApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->RefreshStatus();
        self->RefreshTrustPage();
    }
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* CreateCard(lv_obj_t* parent, lv_coord_t y, lv_coord_t height) {
    auto* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, height);
    lv_obj_set_pos(card, 10, y);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

}  // namespace

std::string ThemesStatusText(const rodakos::AppearanceStatusSnapshot& snapshot) {
    const std::string& status = snapshot.status;
    if (status == "awaiting_trust") return "需要确认发布端";
    if (status == "waiting_device") {
        return snapshot.error.empty() ? "等待下载" : "等待重试：" + snapshot.error;
    }
    if (status == "deferred_busy") return "设备忙，稍后下载";
    if (status == "downloading") {
        char text[32] = {};
        std::snprintf(text, sizeof(text), "下载中 %d%%", snapshot.progress);
        return text;
    }
    if (status == "pending_reboot") return "重启后生效";
    if (status == "failed") return "失败：" + (snapshot.error.empty() ? std::string("unknown") : snapshot.error);
    if (status == "fallback") {
        return "已回退：" +
            (snapshot.fallback_reason.empty() ? std::string("unknown") : snapshot.fallback_reason);
    }
    if (status == "superseded") return "已被新版本取代";
    return "已生效";
}

bool ThemesApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    RefreshStatus();
    ESP_LOGI(TAG, "Themes app created");
    return true;
}

void ThemesApp::OnResume() {
    if (ui_ == nullptr) {
        return;
    }
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    RefreshStatus();
    if (status_timer_ != nullptr) {
        lv_timer_resume(status_timer_);
    }
}

void ThemesApp::OnPause() {
    if (ui_ == nullptr) {
        return;
    }
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return;
    }
    if (status_timer_ != nullptr) {
        lv_timer_pause(status_timer_);
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ThemesApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (context_ != nullptr) {
                lv_async_call_cancel(DeferReturnHome, context_);
                lv_async_call_cancel(DeferRefreshTheme, context_);
            }
            DestroyUi();
        }
    }
    context_ = nullptr;
    ui_ = nullptr;
}

bool ThemesApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    const bool was_hidden = root_ != nullptr && lv_obj_is_valid(root_) &&
                            lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    const ThemesPage page = current_page_;
    DestroyUi();
    CreateUi();
    ShowPage(page);
    if (was_hidden && root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_timer_pause(status_timer_);
    }
    return true;
}

void ThemesApp::DestroyUi() {
    if (status_timer_ != nullptr) {
        lv_timer_delete(status_timer_);
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    ResetUiPointers();
}

void ThemesApp::ResetUiPointers() {
    root_ = nullptr;
    header_title_label_ = nullptr;
    main_body_ = nullptr;
    source_label_ = nullptr;
    revision_label_ = nullptr;
    status_label_ = nullptr;
    trust_label_ = nullptr;
    preset_buttons_.fill(nullptr);
    status_timer_ = nullptr;
    trust_body_ = nullptr;
    fingerprint_label_ = nullptr;
    publisher_status_label_ = nullptr;
    publisher_error_label_ = nullptr;
    refresh_button_ = nullptr;
    confirm_button_ = nullptr;
    displayed_key_id_.clear();
    pressed_key_id_.clear();
    current_page_ = ThemesPage::kMain;
}

void ThemesApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    main_body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(main_body_);
    lv_obj_set_size(main_body_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(main_body_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(main_body_, LV_OBJ_FLAG_SCROLLABLE);

    CreatePreview(main_body_);
    CreatePresetRow(main_body_);
    CreateTrustRow(main_body_);

    // Created after the page bodies so it draws above them and keeps its click area.
    CreateAppHeader(root_, "主题", [](lv_event_t* e) {
        static_cast<ThemesApp*>(lv_event_get_user_data(e))->NavigateBack();
    }, [](lv_event_t* e) {
        static_cast<ThemesApp*>(lv_event_get_user_data(e))->NavigateHome();
    }, this, &header_title_label_);
    status_timer_ = lv_timer_create(StatusTimerCallback, kStatusRefreshMs, this);
}

void ThemesApp::CreatePreview(lv_obj_t* parent) {
    auto* card = CreateCard(parent, kRodakosAppHeaderHeight + 4, 96);
    lv_obj_set_style_pad_all(card, 6, 0);

    auto* frame = lv_obj_create(card);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, kPreviewWidth, kPreviewHeight);
    lv_obj_align(frame, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(frame, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(frame, 6, 0);
    lv_obj_set_style_border_color(frame, rodakos_theme_border(), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_clip_corner(frame, true, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    // The wallpaper is the shared PhoneUi buffer; LVGL scales it while drawing.
    if (const auto* wallpaper = ui_->wallpaper(); wallpaper != nullptr) {
        auto* image = lv_image_create(frame);
        lv_image_set_src(image, wallpaper);
        lv_obj_set_size(image, kPreviewWidth, kPreviewHeight);
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_set_pos(image, 0, 0);
        lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    }

    auto* bar = lv_obj_create(frame);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, kPreviewWidth, 14);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);

    for (int i = 0; i < 3; ++i) {
        auto* tile = lv_obj_create(frame);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, 20, 20);
        lv_obj_set_pos(tile, 12 + i * 32, 24);
        lv_obj_set_style_radius(tile, 6, 0);
        lv_obj_set_style_bg_color(tile, rodakos_theme_icon_color(i), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    }

    auto* button = lv_obj_create(frame);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 48, 14);
    lv_obj_set_pos(button, 12, 58);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);

    auto* sample = CreateText(frame, "Aa", &phone_font_12, rodakos_theme_text_primary());
    lv_obj_set_pos(sample, 70, 55);

    const lv_coord_t text_x = kPreviewWidth + 10;
    const lv_coord_t text_width = 300 - 2 * 6 - text_x;
    source_label_ = CreateText(card, "", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_width(source_label_, text_width);
    lv_label_set_long_mode(source_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(source_label_, text_x, 4);

    revision_label_ = CreateText(card, "", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(revision_label_, text_width);
    lv_label_set_long_mode(revision_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(revision_label_, text_x, 28);

    // Two lines at most; long error codes end in an ellipsis inside the card.
    status_label_ = CreateText(card, "", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_size(status_label_, text_width, 34);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(status_label_, text_x, 48);
}

void ThemesApp::CreatePresetRow(lv_obj_t* parent) {
    auto* card = CreateCard(parent, kRodakosAppHeaderHeight + 104, 44);
    auto* icon = CreateText(card, ICON_PALETTE, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    auto* title = CreateText(card, "配色", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 188, 28);
    lv_obj_align(row, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < kThemePresetOptions.size(); ++i) {
        const auto& option = kThemePresetOptions[i];
        auto* button = lv_btn_create(row);
        preset_buttons_[i] = button;
        lv_obj_set_user_data(button, const_cast<ThemePresetOption*>(&option));
        lv_obj_set_size(button, 42, 26);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(option.swatch), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(button, rodakos_theme_border(), 0);
        lv_obj_set_style_border_width(button, 1, 0);

        auto* label = CreateText(button, option.button_label, &phone_font_12, lv_color_hex(option.label_color));
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(label);

        lv_obj_add_event_cb(button, [](lv_event_t* e) {
            auto* self = static_cast<ThemesApp*>(lv_event_get_user_data(e));
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            const auto* selected = static_cast<const ThemePresetOption*>(lv_obj_get_user_data(target));
            if (selected != nullptr) {
                self->SelectPreset(*selected);
            }
        }, LV_EVENT_CLICKED, this);
    }
}

void ThemesApp::CreateTrustRow(lv_obj_t* parent) {
    auto* card = CreateCard(parent, kRodakosAppHeaderHeight + 152, 42);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    auto* icon = CreateText(card, ICON_LOCK, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    trust_label_ = CreateText(card, "", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_align(trust_label_, LV_ALIGN_LEFT_MID, 28, 0);
    auto* arrow = CreateText(card, ">", &phone_font_18, rodakos_theme_text_tertiary());
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(card, [](lv_event_t* e) {
        static_cast<ThemesApp*>(lv_event_get_user_data(e))->ShowPage(ThemesPage::kTrust);
    }, LV_EVENT_CLICKED, this);
}

void ThemesApp::CreateTrustPage() {
    trust_body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(trust_body_);
    lv_obj_set_size(trust_body_, LV_PCT(100), ui_->height() - kRodakosAppHeaderHeight);
    lv_obj_set_pos(trust_body_, 0, kRodakosAppHeaderHeight);
    lv_obj_set_style_bg_opa(trust_body_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(trust_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(trust_body_, LV_OBJ_FLAG_HIDDEN);

    // Guide text above the fingerprint, status on the title line, buttons at the bottom: fits 200px.
    auto* fingerprint = CreateCard(trust_body_, 4, 108);
    lv_obj_set_style_pad_all(fingerprint, 8, 0);
    auto* guide_label = CreateText(fingerprint,
        "在 Rodak 对照指纹，一致后退出远程控制并在本机触摸确认。",
        &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(guide_label, 284);
    lv_label_set_long_mode(guide_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(guide_label, 0, 0);
    auto* title = CreateText(fingerprint, "发布端指纹", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_pos(title, 0, 38);
    publisher_status_label_ = CreateText(fingerprint, "尚未信任", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(publisher_status_label_, 190);
    lv_obj_set_style_text_align(publisher_status_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(publisher_status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(publisher_status_label_, LV_ALIGN_TOP_RIGHT, 0, 38);
    fingerprint_label_ = CreateText(fingerprint, "尚未读取", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_width(fingerprint_label_, 284);
    lv_label_set_long_mode(fingerprint_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(fingerprint_label_, 0, 56);

    publisher_error_label_ = CreateText(trust_body_, "", &phone_font_12, rodakos_theme_warning());
    lv_obj_set_size(publisher_error_label_, 296, 34);
    lv_label_set_long_mode(publisher_error_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(publisher_error_label_, 12, 116);

    refresh_button_ = lv_btn_create(trust_body_);
    lv_obj_set_size(refresh_button_, 92, 36);
    lv_obj_set_pos(refresh_button_, 10, 156);
    lv_obj_set_style_shadow_width(refresh_button_, 0, 0);
    lv_obj_set_style_bg_color(refresh_button_, rodakos_theme_bg_tertiary(), 0);
    auto* refresh_label = CreateText(refresh_button_, "读取/刷新", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_center(refresh_label);
    lv_obj_add_event_cb(refresh_button_, [](lv_event_t* e) {
        auto* self = static_cast<ThemesApp*>(lv_event_get_user_data(e));
        if (auto* service = self->context_->services().appearance(); service != nullptr) {
            service->RequestPublisher();
            self->RefreshTrustPage();
        }
    }, LV_EVENT_CLICKED, this);

    confirm_button_ = lv_btn_create(trust_body_);
    lv_obj_set_size(confirm_button_, 198, 36);
    lv_obj_set_pos(confirm_button_, 112, 156);
    lv_obj_set_style_shadow_width(confirm_button_, 0, 0);
    lv_obj_set_style_bg_color(confirm_button_, rodakos_theme_primary(), 0);
    auto* confirm_label = CreateText(confirm_button_, "指纹一致，确认信任", &phone_font_14, ui_->theme().accent_text);
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm_button_, [](lv_event_t* e) {
        auto* self = static_cast<ThemesApp*>(lv_event_get_user_data(e));
        const lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED) {
            self->pressed_key_id_ = self->ui_->IsPhysicalInput() ? self->displayed_key_id_ : std::string();
        } else if (code == LV_EVENT_CLICKED) {
            self->ConfirmPublisher();
            self->pressed_key_id_.clear();
        } else if (code == LV_EVENT_PRESS_LOST) {
            self->pressed_key_id_.clear();
        }
    }, LV_EVENT_ALL, this);
}

void ThemesApp::ShowPage(ThemesPage page) {
    if (root_ == nullptr || context_ == nullptr) {
        return;
    }
    current_page_ = page;
    pressed_key_id_.clear();
    if (page == ThemesPage::kTrust) {
        if (trust_body_ == nullptr) {
            CreateTrustPage();
        }
        lv_obj_add_flag(main_body_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(trust_body_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(header_title_label_, "发布端信任");
        if (auto* service = context_->services().appearance(); service != nullptr) {
            service->RequestPublisher();
        }
        RefreshTrustPage();
        return;
    }
    if (trust_body_ != nullptr) {
        lv_obj_add_flag(trust_body_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(main_body_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(header_title_label_, "主题");
    RefreshStatus();
}

void ThemesApp::RefreshStatus() {
    if (root_ == nullptr || context_ == nullptr || current_page_ != ThemesPage::kMain) {
        return;
    }
    auto* appearance = context_->services().appearance();
    const int selected = ThemePresetIndex(ui_->theme_name());
    if (appearance == nullptr) {
        lv_label_set_text(source_label_, "外观服务不可用");
        lv_label_set_text(revision_label_, "");
        lv_label_set_text(status_label_, "");
        lv_label_set_text(trust_label_, "发布端信任");
        for (auto* button : preset_buttons_) {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
        return;
    }

    const auto snapshot = appearance->GetStatusSnapshot();
    lv_label_set_text(source_label_, snapshot.theme_local ? "配色：本地" : "配色：外观包");
    if (snapshot.pending_revision != 0) {
        lv_label_set_text_fmt(revision_label_, "外观包 r%u → r%u",
                              static_cast<unsigned>(snapshot.active_revision),
                              static_cast<unsigned>(snapshot.pending_revision));
    } else if (snapshot.active_revision != 0) {
        lv_label_set_text_fmt(revision_label_, "外观包 r%u", static_cast<unsigned>(snapshot.active_revision));
    } else {
        lv_label_set_text(revision_label_, "内置外观");
    }

    lv_label_set_text(status_label_, ThemesStatusText(snapshot).c_str());
    const bool problem = snapshot.status == "failed" || snapshot.status == "fallback" ||
                         snapshot.status == "awaiting_trust";
    lv_obj_set_style_text_color(status_label_,
        problem ? rodakos_theme_warning() :
        snapshot.status == "pending_reboot" ? rodakos_theme_primary() : rodakos_theme_text_secondary(), 0);

    lv_label_set_text(trust_label_, snapshot.publisher_trusted ? "发布端：已信任" : "发布端：未信任");

    for (size_t i = 0; i < preset_buttons_.size(); ++i) {
        auto* button = preset_buttons_[i];
        const bool active = snapshot.theme_local && static_cast<int>(i) == selected;
        // text_primary contrasts with every swatch; primary matches the blue and green swatches.
        lv_obj_set_style_border_width(button, active ? 2 : 1, 0);
        lv_obj_set_style_border_color(button, active ? rodakos_theme_text_primary() : rodakos_theme_border(), 0);
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    }
}

void ThemesApp::SelectPreset(const ThemePresetOption& option) {
    if (context_ == nullptr || ui_ == nullptr) {
        return;
    }
    if (!SelectLocalThemePreset(context_->services().appearance(), ui_, option)) {
        ui_->ShowToastUnlocked("配色保存失败");
        return;
    }
    ESP_LOGI(TAG, "Local theme preset selected: %s", option.id);
    lv_async_call(DeferRefreshTheme, context_);
}

void ThemesApp::RefreshTrustPage() {
    if (trust_body_ == nullptr || context_ == nullptr || current_page_ != ThemesPage::kTrust) {
        return;
    }
    auto* service = context_->services().appearance();
    if (service == nullptr) {
        lv_label_set_text(publisher_status_label_, "外观服务不可用");
        lv_obj_add_state(confirm_button_, LV_STATE_DISABLED);
        lv_obj_add_state(refresh_button_, LV_STATE_DISABLED);
        return;
    }
    const auto publisher = service->GetPublisherState();
    displayed_key_id_ = publisher.key_id;
    std::string fingerprint = publisher.fingerprint;
    const size_t middle = fingerprint.find(' ', 19);
    if (middle != std::string::npos) fingerprint[middle] = '\n';
    lv_label_set_text(fingerprint_label_, fingerprint.empty() ? "尚未读取" : fingerprint.c_str());
    lv_label_set_text(publisher_status_label_, publisher.loading ? "正在读取发布端..." :
        publisher.trusted ? "已信任此发布端" : "尚未信任");
    lv_obj_set_style_text_color(publisher_status_label_, publisher.trusted ?
        rodakos_theme_success() : rodakos_theme_text_secondary(), 0);
    lv_label_set_text(publisher_error_label_, publisher.error.c_str());
    if (publisher.loading) lv_obj_add_state(refresh_button_, LV_STATE_DISABLED);
    else lv_obj_remove_state(refresh_button_, LV_STATE_DISABLED);
    if (publisher.loading || publisher.trusted || publisher.key_id.empty() ||
        publisher.public_key.empty() || !publisher.error.empty()) {
        lv_obj_add_state(confirm_button_, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(confirm_button_, LV_STATE_DISABLED);
    }
}

void ThemesApp::ConfirmPublisher() {
    if (context_ == nullptr || ui_ == nullptr) return;
    // 远程按下后断开租约会产生释放事件，不能将该释放误认成本机授权。
    if (!ui_->IsPhysicalInput() || pressed_key_id_.empty()) {
        ui_->ShowToastUnlocked("请退出远程控制后在本机确认");
        return;
    }
    auto* service = context_->services().appearance();
    if (service == nullptr || displayed_key_id_.empty()) return;
    const auto publisher = service->GetPublisherState();
    if (publisher.loading || publisher.key_id != displayed_key_id_ ||
        publisher.key_id != pressed_key_id_) {
        RefreshTrustPage();
        ui_->ShowToastUnlocked("发布端已变化，请重新对照指纹");
        return;
    }
    const bool confirmed = service->ConfirmPublisher(displayed_key_id_);
    RefreshTrustPage();
    ui_->ShowToastUnlocked(confirmed ? "发布端已确认" : "保存信任失败，请重试");
}

bool ThemesApp::OnBackRequested() {
    NavigateBack();
    return true;
}

void ThemesApp::NavigateBack() {
    if (current_page_ == ThemesPage::kTrust) {
        ShowPage(ThemesPage::kMain);
        return;
    }
    NavigateHome();
}

void ThemesApp::NavigateHome() {
    if (context_ != nullptr) {
        lv_async_call(DeferReturnHome, context_);
    }
}

void RegisterThemesApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "themes",
        .title = "Themes",
        .icon = ICON_PALETTE,
        .category = PhoneAppCategory::kSystem,
        .icon_tint = PhoneAppIconTint::kWarm,
        .capabilities = PhoneCapability::kNone,
        .show_on_home = true,
        .aliases = {"theme", "appearance", "wallpaper", "主题", "外观", "壁纸"},
        .create = []() { return std::make_unique<ThemesApp>(); },
    });
}
