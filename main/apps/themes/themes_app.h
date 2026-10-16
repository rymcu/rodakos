#pragma once

#include "phone_os/appearance_status.h"
#include "phone_os/phone_app.h"
#include "phone_ui/theme_presets.h"

#include <lvgl.h>

#include <array>
#include <string>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

// Maps an AppearanceService status snapshot to the one-line Themes status text.
std::string ThemesStatusText(const rodakos::AppearanceStatusSnapshot& snapshot);

enum class ThemesPage {
    kMain,
    kTrust,  // 发布端指纹与本机触摸确认
};

class ThemesApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    bool OnBackRequested() override;
    void RefreshStatus();
    void RefreshTrustPage();

private:
    void CreateUi();
    void DestroyUi();
    void ResetUiPointers();
    void CreatePreview(lv_obj_t* parent);
    void CreatePresetRow(lv_obj_t* parent);
    void CreateTrustRow(lv_obj_t* parent);
    void CreateTrustPage();
    void ShowPage(ThemesPage page);
    void SelectPreset(const ThemePresetOption& option);
    void ConfirmPublisher();
    void NavigateBack();
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    ThemesPage current_page_ = ThemesPage::kMain;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* header_title_label_ = nullptr;
    lv_obj_t* main_body_ = nullptr;
    lv_obj_t* source_label_ = nullptr;
    lv_obj_t* revision_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* trust_label_ = nullptr;
    std::array<lv_obj_t*, kThemePresetOptions.size()> preset_buttons_ = {};
    lv_timer_t* status_timer_ = nullptr;

    lv_obj_t* trust_body_ = nullptr;
    lv_obj_t* fingerprint_label_ = nullptr;
    lv_obj_t* publisher_status_label_ = nullptr;
    lv_obj_t* publisher_error_label_ = nullptr;
    lv_obj_t* refresh_button_ = nullptr;
    lv_obj_t* confirm_button_ = nullptr;
    // The key shown when a physical press began; a later release only confirms that key.
    std::string displayed_key_id_;
    std::string pressed_key_id_;
};

void RegisterThemesApp(PhoneAppRegistry& registry);
