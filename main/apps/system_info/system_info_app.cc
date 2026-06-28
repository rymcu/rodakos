#include "apps/system_info/system_info_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {
constexpr const char* TAG = "SystemInfoApp";
constexpr lv_coord_t kHeaderHeight = 40;
constexpr lv_coord_t kBodyTop = 44;
constexpr lv_coord_t kBodyHeight = 192;
constexpr lv_coord_t kCardWidth = 300;
constexpr lv_coord_t kCardHeight = 50;

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        lv_indev_reset(nullptr, nullptr);
        context->navigation().ReturnHome();
    }
}

void RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<SystemInfoApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->Refresh();
    }
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

std::string FormatBytes(uint64_t bytes) {
    char buffer[32] = {};
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%u B", static_cast<unsigned>(bytes));
    } else if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    return buffer;
}

std::string FormatDuration(uint64_t seconds) {
    const uint64_t days = seconds / 86400;
    seconds %= 86400;
    const uint64_t hours = seconds / 3600;
    seconds %= 3600;
    const uint64_t minutes = seconds / 60;
    seconds %= 60;

    char buffer[40] = {};
    if (days > 0) {
        std::snprintf(buffer, sizeof(buffer), "%ud %02u:%02u:%02u",
                      static_cast<unsigned>(days),
                      static_cast<unsigned>(hours),
                      static_cast<unsigned>(minutes),
                      static_cast<unsigned>(seconds));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u",
                      static_cast<unsigned>(hours),
                      static_cast<unsigned>(minutes),
                      static_cast<unsigned>(seconds));
    }
    return buffer;
}

const char* WiFiStatusText(WiFiStatus status) {
    switch (status) {
        case WiFiStatus::kConnected:
            return "Connected";
        case WiFiStatus::kConnecting:
            return "Connecting";
        case WiFiStatus::kFailed:
            return "Failed";
        case WiFiStatus::kDisconnected:
        default:
            return "Disconnected";
    }
}

}  // namespace

SystemInfoApp::~SystemInfoApp() {
    OnDestroy();
}

bool SystemInfoApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    file_service_ = context.services().file_service();

    ProbeStorage(true);

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    Refresh();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, 2000, this);

    ESP_LOGI(TAG, "System info app created");
    return true;
}

void SystemInfoApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (refresh_timer_ != nullptr) {
                lv_timer_delete(refresh_timer_);
                refresh_timer_ = nullptr;
            }
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }

    root_ = nullptr;
    body_ = nullptr;
    wifi_ = {};
    memory_ = {};
    storage_ = {};
    uptime_ = {};
    firmware_ = {};
    chip_ = {};
    context_ = nullptr;
    ui_ = nullptr;
    file_service_ = nullptr;
}

void SystemInfoApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    auto* header = lv_obj_create(root_);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), kHeaderHeight);
    lv_obj_set_style_bg_color(header, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(header, 10, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    auto* back_btn = RodakosCreateHeaderIconButton(header, FONT_AWESOME_ARROW_LEFT);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back_btn, [](lv_event_t* e) {
        auto* self = static_cast<SystemInfoApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, LV_EVENT_CLICKED, this);

    auto* title = CreateText(header, "System", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_center(title);

    auto* home_btn = RodakosCreateHeaderIconButton(header, FONT_AWESOME_HOUSE);
    lv_obj_align(home_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(home_btn, [](lv_event_t* e) {
        auto* self = static_cast<SystemInfoApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, LV_EVENT_CLICKED, this);

    body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(body_);
    lv_obj_set_size(body_, kCardWidth, kBodyHeight);
    lv_obj_set_pos(body_, 10, kBodyTop);
    lv_obj_set_flex_flow(body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body_, 6, 0);
    lv_obj_set_scroll_dir(body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body_, LV_SCROLLBAR_MODE_AUTO);

    wifi_ = CreateInfoCard(body_, FONT_AWESOME_WIFI, "WiFi");
    memory_ = CreateInfoCard(body_, FONT_AWESOME_MICROCHIP_AI, "Memory");
    storage_ = CreateInfoCard(body_, FONT_AWESOME_SD_CARD, "Storage");
    uptime_ = CreateInfoCard(body_, FONT_AWESOME_ARROWS_ROTATE, "Uptime");
    firmware_ = CreateInfoCard(body_, FONT_AWESOME_CIRCLE_INFO, "Firmware");
    chip_ = CreateInfoCard(body_, FONT_AWESOME_SIGNAL, "Hardware");
}

SystemInfoApp::InfoLabels SystemInfoApp::CreateInfoCard(lv_obj_t* parent,
                                                        const char* icon,
                                                        const char* title) {
    auto* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, kCardWidth, kCardHeight);
    lv_obj_set_style_bg_color(card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    auto* icon_label = CreateText(card, icon, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 0, 0);

    auto* title_label = CreateText(card, title, &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 30, 1);

    auto* value_label = CreateText(card, "--", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_width(value_label, 245);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_align(value_label, LV_ALIGN_TOP_LEFT, 30, 17);

    auto* detail_label = CreateText(card, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(detail_label, 245);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
    lv_obj_align(detail_label, LV_ALIGN_TOP_LEFT, 30, 34);

    return InfoLabels{.value = value_label, .detail = detail_label};
}

void SystemInfoApp::Refresh() {
    if (wifi_.value == nullptr) {
        return;
    }

    auto* wifi = context_->services().wifi();
    if (wifi == nullptr) {
        lv_label_set_text(wifi_.value, "Unavailable");
        lv_label_set_text(wifi_.detail, "WiFi service not ready");
    } else {
        const WiFiStatus status = wifi->GetStatus();
        if (status == WiFiStatus::kConnected) {
            const std::string ssid = wifi->GetConnectedSSID();
            const std::string ip = wifi->GetIPAddress();
            lv_label_set_text(wifi_.value, ssid.empty() ? "Connected" : ssid.c_str());
            lv_label_set_text_fmt(wifi_.detail, "IP %s", ip.empty() ? "waiting" : ip.c_str());
        } else {
            lv_label_set_text(wifi_.value, WiFiStatusText(status));
            lv_label_set_text(wifi_.detail, "No active IP address");
        }
    }

    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const std::string internal_text = FormatBytes(internal_free) + " / " + FormatBytes(internal_total);
    const std::string psram_text = "PSRAM " + FormatBytes(psram_free) + " / " + FormatBytes(psram_total);
    lv_label_set_text(memory_.value, internal_text.c_str());
    lv_label_set_text(memory_.detail, psram_text.c_str());

    ProbeStorage(false);
    if (storage_mounted_) {
        const std::string free_text = FormatBytes(storage_capacity_.free_bytes) + " free";
        const std::string total_text = "Total " + FormatBytes(storage_capacity_.total_bytes);
        lv_label_set_text(storage_.value, free_text.c_str());
        lv_label_set_text(storage_.detail, total_text.c_str());
    } else if (storage_checked_) {
        lv_label_set_text(storage_.value, "SD card not mounted");
        lv_label_set_text(storage_.detail, "Storage service idle");
    } else {
        lv_label_set_text(storage_.value, "Unavailable");
        lv_label_set_text(storage_.detail, "File service not ready");
    }

    const uint64_t uptime_seconds = static_cast<uint64_t>(esp_timer_get_time() / 1000000LL);
    const std::string uptime_text = FormatDuration(uptime_seconds);
    lv_label_set_text(uptime_.value, uptime_text.c_str());
    lv_label_set_text(uptime_.detail, "Since last boot");

    const esp_app_desc_t* app = esp_app_get_description();
    if (app != nullptr) {
        lv_label_set_text_fmt(firmware_.value, "%s %s", app->project_name, app->version);
        lv_label_set_text_fmt(firmware_.detail, "%s %s", app->date, app->time);
    }

    esp_chip_info_t chip_info = {};
    esp_chip_info(&chip_info);
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    lv_label_set_text_fmt(chip_.value, "ESP32-S3 rev %u", static_cast<unsigned>(chip_info.revision));
    lv_label_set_text_fmt(chip_.detail, "%u cores, %s flash, IDF %s",
                          static_cast<unsigned>(chip_info.cores),
                          FormatBytes(flash_size).c_str(),
                          esp_get_idf_version());
}

void SystemInfoApp::ProbeStorage(bool allow_mount) {
    storage_checked_ = file_service_ != nullptr;
    storage_mounted_ = false;
    storage_capacity_ = {};

    if (file_service_ == nullptr) {
        return;
    }

    bool mounted = file_service_->IsMounted();
    if (!mounted && allow_mount) {
        mounted = file_service_->Init();
    }
    if (!mounted) {
        return;
    }

    storage_mounted_ = true;
    if (!file_service_->GetCapacity(storage_capacity_)) {
        storage_capacity_ = {};
    }
}

void SystemInfoApp::NavigateHome() {
    if (auto* indev = lv_indev_active(); indev != nullptr) {
        lv_indev_wait_release(indev);
    }
    ESP_LOGI(TAG, "Returning home");
    lv_async_call(DeferReturnHome, context_);
}

void RegisterSystemInfoApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "system",
        .title = "System",
        .icon = FONT_AWESOME_CIRCLE_INFO,
        .category = PhoneAppCategory::kSystem,
        .launch_mode = PhoneAppLaunchMode::kReplaceCurrent,
        .capabilities = PhoneCapability::kStorage | PhoneCapability::kNetwork | PhoneCapability::kBackgroundTick,
        .show_on_home = true,
        .aliases = {"info", "status", "device", "系统", "状态"},
        .create = []() { return std::make_unique<SystemInfoApp>(); },
    });
}
