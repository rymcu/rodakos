#include "apps/system_info/system_info_app.h"

#include "phone_os/audio_focus_service.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"
#include "phone_os/button_binding_service.h"
#include "phone_os/camera_service.h"
#include "phone_os/motion_service.h"
#include "phone_os/music_player_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/voice_assistant_service.h"
#include "phone_os/voice_wake_service.h"
#include "phone_os/web_file_system_service.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <esp_app_desc.h>
#include <esp_board_manager.h>
#include <esp_board_manager_defs.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace {
constexpr const char* TAG = "SystemInfoApp";
constexpr lv_coord_t kBodyTop = 44;
constexpr lv_coord_t kBodyHeight = 192;
constexpr lv_coord_t kCardWidth = 300;
constexpr lv_coord_t kCardHeight = 56;

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
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

void FormatBytes(uint64_t bytes, char* buffer, size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0) {
        return;
    }
    if (bytes < 1024) {
        std::snprintf(buffer, buffer_size, "%u B", static_cast<unsigned>(bytes));
    } else if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(buffer, buffer_size, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(buffer, buffer_size, "%.1f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buffer, buffer_size, "%.1f GB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
}

void FormatDuration(uint64_t seconds, char* buffer, size_t buffer_size) {
    if (buffer == nullptr || buffer_size == 0) {
        return;
    }
    const uint64_t days = seconds / 86400;
    seconds %= 86400;
    const uint64_t hours = seconds / 3600;
    seconds %= 3600;
    const uint64_t minutes = seconds / 60;
    seconds %= 60;

    if (days > 0) {
        std::snprintf(buffer, buffer_size, "%ud %02u:%02u:%02u",
                      static_cast<unsigned>(days),
                      static_cast<unsigned>(hours),
                      static_cast<unsigned>(minutes),
                      static_cast<unsigned>(seconds));
    } else {
        std::snprintf(buffer, buffer_size, "%02u:%02u:%02u",
                      static_cast<unsigned>(hours),
                      static_cast<unsigned>(minutes),
                      static_cast<unsigned>(seconds));
    }
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

const char* AudioPlaybackStatusText(rodakos::AudioPlaybackStatus status) {
    switch (status) {
        case rodakos::AudioPlaybackStatus::kLoading:
            return "Loading";
        case rodakos::AudioPlaybackStatus::kPlaying:
            return "Playing";
        case rodakos::AudioPlaybackStatus::kPaused:
            return "Paused";
        case rodakos::AudioPlaybackStatus::kStopped:
            return "Stopped";
        case rodakos::AudioPlaybackStatus::kCompleted:
            return "Completed";
        case rodakos::AudioPlaybackStatus::kError:
            return "Error";
        case rodakos::AudioPlaybackStatus::kIdle:
        default:
            return "Idle";
    }
}

const char* AudioFocusGainText(rodakos::AudioFocusGain gain) {
    switch (gain) {
        case rodakos::AudioFocusGain::kDuck:
            return "duck";
        case rodakos::AudioFocusGain::kPause:
            return "pause";
        case rodakos::AudioFocusGain::kExclusive:
            return "exclusive";
        default:
            return "unknown";
    }
}

const char* VoicePhaseText(rodakos::VoiceAssistantPhase phase) {
    switch (phase) {
        case rodakos::VoiceAssistantPhase::kConnecting:
            return "Connecting";
        case rodakos::VoiceAssistantPhase::kListening:
            return "Listening";
        case rodakos::VoiceAssistantPhase::kSpeaking:
            return "Speaking";
        case rodakos::VoiceAssistantPhase::kError:
            return "Error";
        case rodakos::VoiceAssistantPhase::kIdle:
        default:
            return "Idle";
    }
}

const char* VoiceWakeStatusText(rodakos::VoiceWakeStatus status) {
    switch (status) {
        case rodakos::VoiceWakeStatus::kListening:
            return "Listening";
        case rodakos::VoiceWakeStatus::kAssistantActive:
            return "Assistant";
        case rodakos::VoiceWakeStatus::kUnavailable:
            return "Unavailable";
        case rodakos::VoiceWakeStatus::kError:
            return "Error";
        case rodakos::VoiceWakeStatus::kDisabled:
        default:
            return "Disabled";
    }
}

std::string ShortAppLabel(const std::string& id) {
    return id.empty() ? std::string("none") : id;
}

const char* YesNo(bool value) {
    return value ? "yes" : "no";
}

uint32_t CapabilityBits(PhoneCapability capabilities) {
    return static_cast<uint32_t>(capabilities);
}

}  // namespace

SystemInfoApp::~SystemInfoApp() {
    OnDestroy();
}

bool SystemInfoApp::OnCreate(PhoneAppContext& context) {
    BindServices(context);

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
            DestroyUi();
        }
    }

    context_ = nullptr;
    ui_ = nullptr;
    file_service_ = nullptr;
    audio_focus_service_ = nullptr;
    audio_output_service_ = nullptr;
    audio_service_ = nullptr;
    button_service_ = nullptr;
    camera_service_ = nullptr;
    motion_service_ = nullptr;
    music_player_service_ = nullptr;
    voice_assistant_service_ = nullptr;
    voice_wake_service_ = nullptr;
    web_files_service_ = nullptr;
}

bool SystemInfoApp::OnThemeChanged(PhoneAppContext& context) {
    BindServices(context);

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    const bool was_hidden = root_ != nullptr && lv_obj_is_valid(root_) &&
                            lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    DestroyUi();
    CreateUi();
    Refresh();
    if (was_hidden && root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, 2000, this);
    return true;
}

void SystemInfoApp::BindServices(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    auto& services = context.services();
    file_service_ = services.file_service();
    audio_focus_service_ = services.audio_focus();
    audio_output_service_ = services.audio_output();
    audio_service_ = services.audio();
    button_service_ = services.buttons();
    camera_service_ = services.camera();
    motion_service_ = services.motion();
    music_player_service_ = services.music_player();
    voice_assistant_service_ = services.voice_assistant();
    voice_wake_service_ = services.voice_wake();
    web_files_service_ = services.web_files();
}

void SystemInfoApp::DestroyUi() {
    if (refresh_timer_ != nullptr) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    ResetUiPointers();
}

void SystemInfoApp::ResetUiPointers() {
    root_ = nullptr;
    body_ = nullptr;
    wifi_ = {};
    memory_ = {};
    storage_ = {};
    uptime_ = {};
    firmware_ = {};
    chip_ = {};
    heap_detail_ = {};
    runtime_ = {};
    buses_ = {};
    camera_ = {};
    audio_ = {};
    motion_ = {};
    web_ = {};
    voice_ = {};
    buttons_ = {};
}

void SystemInfoApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "System", [](lv_event_t* e) {
        auto* self = static_cast<SystemInfoApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, [](lv_event_t* e) {
        auto* self = static_cast<SystemInfoApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this);

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
    memory_ = CreateInfoCard(body_, FONT_AWESOME_MICROCHIP_AI, "Heap free/total");
    storage_ = CreateInfoCard(body_, FONT_AWESOME_SD_CARD, "Storage");
    uptime_ = CreateInfoCard(body_, FONT_AWESOME_ARROWS_ROTATE, "Uptime");
    firmware_ = CreateInfoCard(body_, FONT_AWESOME_CIRCLE_INFO, "Firmware");
    chip_ = CreateInfoCard(body_, FONT_AWESOME_SIGNAL, "Hardware");
    heap_detail_ = CreateInfoCard(body_, FONT_AWESOME_CIRCLE_INFO, "Largest block");
    runtime_ = CreateInfoCard(body_, FONT_AWESOME_MICROCHIP_AI, "Runtime");
    buses_ = CreateInfoCard(body_, FONT_AWESOME_LINK, "Buses");
    camera_ = CreateInfoCard(body_, FONT_AWESOME_CAMERA, "Camera");
    audio_ = CreateInfoCard(body_, FONT_AWESOME_VOLUME_HIGH, "Audio");
    motion_ = CreateInfoCard(body_, FONT_AWESOME_COMPASS, "Motion");
    web_ = CreateInfoCard(body_, FONT_AWESOME_CLOUD, "Web files");
    voice_ = CreateInfoCard(body_, FONT_AWESOME_MICROPHONE, "Voice");
    buttons_ = CreateInfoCard(body_, FONT_AWESOME_KEY, "Buttons");
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
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    auto* icon_label = CreateText(card, icon, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 8, 0);

    auto* title_label = CreateText(card, title, &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 38, 6);

    auto* value_label = CreateText(card, "--", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_width(value_label, 248);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_align(value_label, LV_ALIGN_TOP_LEFT, 38, 21);

    auto* detail_label = CreateText(card, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(detail_label, 248);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
    lv_obj_align(detail_label, LV_ALIGN_TOP_LEFT, 38, 38);

    return InfoLabels{.value = value_label, .detail = detail_label};
}

void SystemInfoApp::Refresh() {
    if (wifi_.value == nullptr) {
        return;
    }

    RefreshWiFi();
    RefreshMemory();
    RefreshStorage();
    RefreshRuntime();
    RefreshHardware();
    RefreshVoice();
    RefreshFirmware();
}

void SystemInfoApp::RefreshWiFi() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr) {
        lv_label_set_text(wifi_.value, "Unavailable");
        lv_label_set_text(wifi_.detail, "WiFi service not ready");
    } else {
        const WiFiStatus status = wifi->GetStatus();
        if (status == WiFiStatus::kConnected) {
            const auto ssid = wifi->GetConnectedSSID();
            const auto ip = wifi->GetIPAddress();
            lv_label_set_text(wifi_.value, ssid.empty() ? "Connected" : ssid.c_str());
            lv_label_set_text_fmt(wifi_.detail, "IP %s", ip.empty() ? "waiting" : ip.c_str());
        } else {
            lv_label_set_text(wifi_.value, WiFiStatusText(status));
            lv_label_set_text(wifi_.detail, "No active IP address");
        }
    }
}

void SystemInfoApp::RefreshMemory() {
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    char internal_free_text[24] = {};
    char internal_total_text[24] = {};
    char psram_free_text[24] = {};
    char psram_total_text[24] = {};
    char internal_largest_text[24] = {};
    char psram_largest_text[24] = {};
    char value_text[80] = {};
    char detail_text[96] = {};
    FormatBytes(internal_free, internal_free_text, sizeof(internal_free_text));
    FormatBytes(internal_total, internal_total_text, sizeof(internal_total_text));
    FormatBytes(psram_free, psram_free_text, sizeof(psram_free_text));
    FormatBytes(psram_total, psram_total_text, sizeof(psram_total_text));
    FormatBytes(internal_largest, internal_largest_text, sizeof(internal_largest_text));
    FormatBytes(psram_largest, psram_largest_text, sizeof(psram_largest_text));

    std::snprintf(value_text, sizeof(value_text), "SRAM %s/%s",
                  internal_free_text, internal_total_text);
    std::snprintf(detail_text, sizeof(detail_text), "PSRAM %s/%s",
                  psram_free_text, psram_total_text);
    lv_label_set_text(memory_.value, value_text);
    lv_label_set_text(memory_.detail, detail_text);

    std::snprintf(value_text, sizeof(value_text), "SRAM block %s", internal_largest_text);
    std::snprintf(detail_text, sizeof(detail_text), "PSRAM block %s; chip 8 MB", psram_largest_text);
    lv_label_set_text(heap_detail_.value, value_text);
    lv_label_set_text(heap_detail_.detail, detail_text);
}

void SystemInfoApp::RefreshStorage() {
    ProbeStorage(false);
    char value_text[80] = {};
    char detail_text[96] = {};
    if (storage_mounted_) {
        char storage_free_text[24] = {};
        char storage_total_text[24] = {};
        FormatBytes(storage_capacity_.free_bytes, storage_free_text, sizeof(storage_free_text));
        FormatBytes(storage_capacity_.total_bytes, storage_total_text, sizeof(storage_total_text));
        std::snprintf(value_text, sizeof(value_text), "%s free", storage_free_text);
        std::snprintf(detail_text, sizeof(detail_text), "Total %s", storage_total_text);
        lv_label_set_text(storage_.value, value_text);
        lv_label_set_text(storage_.detail, detail_text);
    } else if (storage_checked_) {
        lv_label_set_text(storage_.value, "SD card not mounted");
        lv_label_set_text(storage_.detail, "Storage service idle");
    } else {
        lv_label_set_text(storage_.value, "Unavailable");
        lv_label_set_text(storage_.detail, "File service not ready");
    }
}

void SystemInfoApp::RefreshRuntime() {
    const auto host = context_->navigation().GetAppHostState();
    const std::string current = host.has_current ? ShortAppLabel(host.current_app_id) : "none";
    const std::string background = host.has_background ? ShortAppLabel(host.background_app_id) : "none";
    lv_label_set_text_fmt(runtime_.value, "Current %s", current.c_str());
    lv_label_set_text_fmt(runtime_.detail, "Background %s, cap 0x%02x/0x%02x",
                          background.c_str(),
                          static_cast<unsigned>(CapabilityBits(host.current_capabilities)),
                          static_cast<unsigned>(CapabilityBits(host.background_capabilities)));
}

void SystemInfoApp::RefreshHardware() {
    const bool has_i2c_config = esp_board_manager_check_name(ESP_BOARD_PERIPH_NAME_I2C_MASTER);
    lv_label_set_text_fmt(buses_.value, "I2C %s", has_i2c_config ? "declared" : "missing");
    lv_label_set_text_fmt(buses_.detail, "SD %s, diagnostics read-only",
                          storage_mounted_ ? "mounted" : "not mounted");

    if (camera_service_ == nullptr) {
        lv_label_set_text(camera_.value, "Unavailable");
        lv_label_set_text(camera_.detail, "Camera service not ready");
    } else {
        const auto state = camera_service_->GetState();
        lv_label_set_text_fmt(camera_.value, "%s%s",
                              state.available ? "Configured" : "Unavailable",
                              state.preview_running ? ", preview" : "");
        lv_label_set_text_fmt(camera_.detail, "%ux%u frames=%u %s",
                              static_cast<unsigned>(state.width),
                              static_cast<unsigned>(state.height),
                              static_cast<unsigned>(state.frame_count),
                              state.last_error.empty() ? "" : state.last_error.c_str());
    }

    const auto audio_state =
        audio_service_ != nullptr ? audio_service_->GetState() : rodakos::AudioPlaybackState{};
    const auto focus_state = audio_focus_service_ != nullptr ? audio_focus_service_->GetState()
                                                             : rodakos::AudioFocusState{};
    lv_label_set_text_fmt(audio_.value, "%s, out %s",
                          audio_service_ != nullptr ? AudioPlaybackStatusText(audio_state.status)
                                                    : "Unavailable",
                          audio_output_service_ != nullptr && audio_output_service_->IsOpen() ? "open"
                                                                                               : "closed");
    if (focus_state.active) {
        lv_label_set_text_fmt(audio_.detail, "focus %s:%s vol=%d",
                              focus_state.owner.c_str(),
                              AudioFocusGainText(focus_state.gain),
                              audio_state.volume);
    } else {
        lv_label_set_text_fmt(audio_.detail, "focus none, tracks %u, vol=%d",
                              static_cast<unsigned>(music_player_service_ != nullptr
                                                        ? music_player_service_->track_count()
                                                        : 0),
                              audio_state.volume);
    }

    if (motion_service_ == nullptr) {
        lv_label_set_text(motion_.value, "Unavailable");
        lv_label_set_text(motion_.detail, "Motion service not ready");
    } else {
        const auto state = motion_service_->GetState();
        lv_label_set_text_fmt(motion_.value, "%s %s",
                              state.sensor_name.c_str(),
                              state.active ? "active" : "idle");
        lv_label_set_text_fmt(motion_.detail, "clients=%u %s",
                              static_cast<unsigned>(state.active_clients),
                              state.last_error.empty() ? "" : state.last_error.c_str());
    }

    if (button_service_ == nullptr) {
        lv_label_set_text(buttons_.value, "Unavailable");
        lv_label_set_text(buttons_.detail, "Button service not ready");
    } else {
        const auto& buttons = button_service_->ListButtons();
        lv_label_set_text_fmt(buttons_.value, "%u board buttons",
                              static_cast<unsigned>(buttons.size()));
        if (buttons.empty()) {
            lv_label_set_text(buttons_.detail, "No board buttons discovered");
        } else {
            lv_label_set_text_fmt(buttons_.detail, "%s=%s",
                                  buttons[0].id.c_str(),
                                  YesNo(buttons[0].available));
        }
    }
}

void SystemInfoApp::RefreshVoice() {
    if (web_files_service_ == nullptr) {
        lv_label_set_text(web_.value, "Unavailable");
        lv_label_set_text(web_.detail, "Web file service not ready");
    } else {
        const auto state = web_files_service_->GetState();
        lv_label_set_text(web_.value, state.running ? "Running" : "Stopped");
        lv_label_set_text_fmt(web_.detail, "%s %s",
                              state.busy ? "busy" : "idle",
                              state.url.empty() ? state.message.c_str() : state.url.c_str());
    }

    const auto assistant_state = voice_assistant_service_ != nullptr
                                     ? voice_assistant_service_->GetState()
                                     : rodakos::VoiceAssistantState{};
    const auto wake_state = voice_wake_service_ != nullptr
                                ? voice_wake_service_->GetState()
                                : rodakos::VoiceWakeState{};
    lv_label_set_text_fmt(voice_.value, "Assistant %s", VoicePhaseText(assistant_state.phase));
    lv_label_set_text_fmt(voice_.detail, "wake %s, rec %s, tx %s",
                          VoiceWakeStatusText(wake_state.status),
                          assistant_state.recorder_name.empty() ? "n/a" : assistant_state.recorder_name.c_str(),
                          assistant_state.transport_name.empty() ? "n/a" : assistant_state.transport_name.c_str());
}

void SystemInfoApp::RefreshFirmware() {
    char value_text[80] = {};
    const uint64_t uptime_seconds = static_cast<uint64_t>(esp_timer_get_time() / 1000000LL);
    FormatDuration(uptime_seconds, value_text, sizeof(value_text));
    lv_label_set_text(uptime_.value, value_text);
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
    char flash_size_text[24] = {};
    FormatBytes(flash_size, flash_size_text, sizeof(flash_size_text));
    lv_label_set_text_fmt(chip_.value, "ESP32-S3 rev %u", static_cast<unsigned>(chip_info.revision));
    lv_label_set_text_fmt(chip_.detail, "%u cores, %s flash, IDF %s",
                          static_cast<unsigned>(chip_info.cores),
                          flash_size_text,
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
