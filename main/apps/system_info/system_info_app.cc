#include "apps/system_info/system_info_app.h"

#include "phone_os/audio_focus_service.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"
#include "phone_os/battery_monitor.h"
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
#include "phone_ui/rodakos_list.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/wifi_adapter.h"

#include <esp_app_desc.h>
#include <esp_board_manager.h>
#include <esp_board_manager_defs.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace rodakos_ui;

namespace {
constexpr const char* TAG = "SystemInfoApp";
constexpr const char* kTitle = "关于本机";
constexpr const char* kBoardName = "RYMCU BigSmart";
constexpr uint32_t kRefreshMs = 2000;
// Fixed rows of the 320x200 body; group y positions follow from them.
constexpr lv_coord_t kIdentityHeight = 52;
constexpr lv_coord_t kResourceRowHeight = 40;
constexpr lv_coord_t kWiFiRowHeight = 48;
constexpr lv_coord_t kDetailRowHeight = 28;
// Used share above which a resource bar turns to the warning colour.
constexpr int kResourceWarnPercent = 85;
constexpr int kBatteryLowPercent = 20;

struct ServiceRowSpec {
    SystemInfoApp::Service service;
    const char* icon;
    const char* title;
    // Heading of the detail card: the hardware or component behind the service. Chips come from
    // components/rodakos_hal_boards/boards/rymcu/rymcu_bigsmart/board_devices.yaml.
    const char* hardware;
};
constexpr ServiceRowSpec kServiceRows[] = {
    {SystemInfoApp::Service::kCamera, ICON_CAMERA, "相机", "GC0308 · DVP"},
    {SystemInfoApp::Service::kAudio, ICON_VOLUME, "音频", "ES8311 扬声器"},
    {SystemInfoApp::Service::kWake, ICON_MICROPHONE, "语音唤醒", nullptr},
    {SystemInfoApp::Service::kAssistant, ICON_ASSISTANT, "语音助手", "实时语音"},
    {SystemInfoApp::Service::kMotion, ICON_COMPASS, "运动传感器", nullptr},
    {SystemInfoApp::Service::kButtons, ICON_KEY, "按键", "板载按键"},
    {SystemInfoApp::Service::kWebFiles, ICON_FOLDER, "网页传文件", "局域网 HTTP"},
};
constexpr ServiceRowSpec kDeveloperRow = {SystemInfoApp::Service::kDeveloper, ICON_CHIP, "开发信息",
                                          "运行时与总线"};

const ServiceRowSpec& Spec(SystemInfoApp::Service service) {
    for (const auto& spec : kServiceRows) {
        if (spec.service == service) return spec;
    }
    return kDeveloperRow;
}

const char* ServiceTitle(SystemInfoApp::Service service) { return Spec(service).title; }

const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// esp_app_desc dates are __DATE__ ("Nov 21 2026") and __TIME__ ("18:02:11"); show 2026-11-21 18:02.
void FormatBuildTime(const char* date, const char* time, char* out, size_t size) {
    char month[4] = {};
    int day = 0;
    int year = 0;
    int month_index = 0;
    if (std::sscanf(date, "%3s %d %d", month, &day, &year) == 3) {
        for (int i = 0; i < 12; ++i) {
            if (std::strcmp(month, kMonths[i]) == 0) month_index = i + 1;
        }
    }
    if (month_index == 0) {
        std::snprintf(out, size, "%s %s", date, time);
        return;
    }
    std::snprintf(out, size, "%04d-%02d-%02d %.5s", year, month_index, day, time);
}

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().ReturnHome();
}

// Back from 关于本机: to Settings when Settings opened it, otherwise to the desktop.
void DeferReturnToCaller(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().ReturnToCaller();
}

void RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<SystemInfoApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->Refresh();
}

void FormatBytes(uint64_t bytes, char* buffer, size_t buffer_size) {
    if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(buffer, buffer_size, "%u KB", static_cast<unsigned>(bytes / 1024ULL));
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(buffer, buffer_size, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buffer, buffer_size, "%.1f GB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
}

void FormatUptime(uint64_t seconds, char* buffer, size_t buffer_size) {
    const unsigned days = static_cast<unsigned>(seconds / 86400);
    const unsigned hours = static_cast<unsigned>(seconds % 86400 / 3600);
    const unsigned minutes = static_cast<unsigned>(seconds % 3600 / 60);
    if (days > 0) {
        std::snprintf(buffer, buffer_size, "%u 天 %02u:%02u", days, hours, minutes);
    } else {
        std::snprintf(buffer, buffer_size, "%02u:%02u:%02u", hours, minutes,
                      static_cast<unsigned>(seconds % 60));
    }
}

lv_obj_t* CreateNumberLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

// 8 px status dot; hollow for "off" so the state does not rely on colour alone.
lv_obj_t* CreateDot(lv_obj_t* parent) {
    auto* dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(dot, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    return dot;
}

const char* AppName(PhoneAppContext* context, const std::string& id) {
    if (id.empty()) return "无";
    const PhoneAppDescriptor* app = context != nullptr ? context->registry().FindById(id) : nullptr;
    return app != nullptr ? app->title.c_str() : id.c_str();
}

const char* FocusGainText(rodakos::AudioFocusGain gain) {
    switch (gain) {
        case rodakos::AudioFocusGain::kDuck: return "duck";
        case rodakos::AudioFocusGain::kExclusive: return "exclusive";
        case rodakos::AudioFocusGain::kPause:
        default: return "pause";
    }
}

}  // namespace

bool SystemInfoApp::OnCreate(PhoneAppContext& context) {
    BindServices(context);
    storage_snapshot_ = std::make_shared<StorageSnapshot>();
    // Draw first; the card may take a while to mount and report free space.
    RequestStorageProbe(true);

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;
    CreateUi();
    Refresh();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kRefreshMs, this);
    ESP_LOGI(TAG, "System info app created");
    return true;
}

void SystemInfoApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) DestroyUi();
    }
    context_ = nullptr;
    ui_ = nullptr;
    // A probe still running keeps its own reference to the snapshot.
    storage_snapshot_.reset();
    file_service_ = nullptr;
    audio_focus_service_ = nullptr;
    audio_output_service_ = nullptr;
    audio_service_ = nullptr;
    battery_ = nullptr;
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
    if (!lock.locked()) return false;
    const bool was_hidden = root_ != nullptr && lv_obj_is_valid(root_) &&
                            lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    const Service open_detail = detail_service_;
    const lv_coord_t scroll_y = body_ != nullptr ? lv_obj_get_scroll_y(body_) : 0;
    DestroyUi();
    CreateUi();
    lv_obj_update_layout(body_);
    lv_obj_scroll_to_y(body_, scroll_y, LV_ANIM_OFF);
    if (open_detail != Service::kCount) OpenDetail(open_detail);
    Refresh();
    if (was_hidden && root_ != nullptr) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kRefreshMs, this);
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
    battery_ = services.battery();
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
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    ResetUiPointers();
}

void SystemInfoApp::ResetUiPointers() {
    root_ = nullptr;
    title_label_ = nullptr;
    body_ = nullptr;
    sram_ = {};
    psram_ = {};
    storage_ = {};
    wifi_name_ = nullptr;
    wifi_detail_ = nullptr;
    battery_value_ = nullptr;
    uptime_value_ = nullptr;
    for (auto& row : services_) row = {};
    detail_service_ = Service::kCount;
    detail_ = nullptr;
    detail_heading_ = nullptr;
    detail_dot_ = nullptr;
    detail_status_ = nullptr;
    for (int i = 0; i < kDetailRows; ++i) {
        detail_keys_[i] = nullptr;
        detail_values_[i] = nullptr;
    }
    detail_raw_ = nullptr;
}

void SystemInfoApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, kTitle, [](lv_event_t* e) {
        static_cast<SystemInfoApp*>(lv_event_get_user_data(e))->NavigateBack();
    }, [](lv_event_t* e) {
        static_cast<SystemInfoApp*>(lv_event_get_user_data(e))->NavigateHome();
    }, this, &title_label_);

    body_ = lv_obj_create(root_);
    lv_obj_remove_style_all(body_);
    lv_obj_set_size(body_, LV_PCT(100), LV_VER_RES - kRodakosAppHeaderHeight);
    lv_obj_set_pos(body_, 0, kRodakosAppHeaderHeight);
    lv_obj_add_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(body_, 6, 0);
    CreateMainList();
}

void SystemInfoApp::CreateMainList() {
    lv_coord_t y = kGroupGap;

    // ===== Identity: board, system version and chip =====
    auto* identity = CreateGroupList(body_, kGridX, y, kGridWidth, kIdentityHeight);
    auto* tile = lv_obj_create(identity);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, 40, 40);
    lv_obj_set_pos(tile, 8, 6);
    lv_obj_set_style_radius(tile, 10, 0);
    lv_obj_set_style_bg_color(tile, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    auto* tile_icon = CreateNumberLabel(tile, PhoneIconFont(), rodakos_theme_on_primary());
    lv_label_set_text(tile_icon, ICON_CHIP);
    lv_obj_center(tile_icon);
    auto* name = CreateNumberLabel(identity, &phone_font_18, rodakos_theme_text_primary());
    lv_label_set_text(name, kBoardName);
    lv_obj_set_pos(name, 60, 7);
    auto* version = CreateNumberLabel(identity, &phone_font_12, rodakos_theme_text_secondary());
    const esp_app_desc_t* app = esp_app_get_description();
    lv_label_set_text_fmt(version, "RodakOS %s · ESP32-S3", app != nullptr ? app->version : "");
    lv_obj_set_pos(version, 60, 30);
    y += kIdentityHeight;

    // ===== 资源 =====
    CreateGroupCaption(body_, "资源", y);
    auto* resources = CreateGroupList(body_, kGridX, y, kGridWidth, kResourceRowHeight * 3);
    const struct {
        const char* icon;
        const char* title;
        ResourceRow* out;
    } resource_rows[] = {
        {ICON_CHIP, "内存", &sram_},
        {ICON_CHIP, "PSRAM", &psram_},
        {ICON_STORAGE, "SD 卡", &storage_},
    };
    int index = 0;
    for (const auto& spec : resource_rows) {
        auto* row = CreateListRow(resources, index++, kGridWidth, kResourceRowHeight, false);
        CreateListIcon(row, spec.icon);
        auto* title = CreateListLabel(row, spec.title);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, kRowTextX, 2);
        spec.out->value = CreateNumberLabel(row, &phone_font_12, rodakos_theme_text_secondary());
        lv_obj_align(spec.out->value, LV_ALIGN_TOP_RIGHT, 0, 5);
        spec.out->bar = lv_bar_create(row);
        lv_obj_set_size(spec.out->bar, kGridWidth - 24 - kRowTextX, 4);
        lv_obj_align(spec.out->bar, LV_ALIGN_TOP_LEFT, kRowTextX, 28);
        lv_bar_set_range(spec.out->bar, 0, 100);
        lv_obj_set_style_radius(spec.out->bar, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(spec.out->bar, 2, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(spec.out->bar, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(spec.out->bar, rodakos_theme_primary(), LV_PART_INDICATOR);
    }
    y += kResourceRowHeight * 3 + kGroupGap;

    // ===== 网络与电源 =====
    CreateGroupCaption(body_, "网络与电源", y);
    auto* network = CreateGroupList(body_, kGridX, y, kGridWidth, kWiFiRowHeight + kRowHeight * 2);
    auto* wifi_row = CreateListRowAt(network, 0, kWiFiRowHeight, true, false);
    CreateListIcon(wifi_row, ICON_WIFI);
    auto* wifi_title = CreateListLabel(wifi_row, "Wi-Fi");
    lv_obj_align(wifi_title, LV_ALIGN_LEFT_MID, kRowTextX, 0);
    wifi_name_ = CreateRowValue(wifi_row, 160, false);
    lv_obj_set_style_text_font(wifi_name_, &phone_font_14, 0);
    lv_obj_set_style_text_color(wifi_name_, rodakos_theme_text_primary(), 0);
    lv_obj_align(wifi_name_, LV_ALIGN_TOP_RIGHT, 0, 4);
    wifi_detail_ = CreateRowValue(wifi_row, 180, false);
    lv_obj_set_style_text_color(wifi_detail_, rodakos_theme_text_tertiary(), 0);
    lv_obj_align(wifi_detail_, LV_ALIGN_TOP_RIGHT, 0, 27);

    auto* battery_row = CreateListRowAt(network, kWiFiRowHeight, kRowHeight, false, false);
    CreateListIcon(battery_row, ICON_BATTERY_FULL);
    lv_obj_align(CreateListLabel(battery_row, "电池"), LV_ALIGN_LEFT_MID, kRowTextX, 0);
    battery_value_ = CreateRowValue(battery_row, 150, false);
    auto* uptime_row = CreateListRowAt(network, kWiFiRowHeight + kRowHeight, kRowHeight, false, false);
    CreateListIcon(uptime_row, ICON_CLOCK);
    lv_obj_align(CreateListLabel(uptime_row, "已运行"), LV_ALIGN_LEFT_MID, kRowTextX, 0);
    uptime_value_ = CreateRowValue(uptime_row, 150, false);
    y += kWiFiRowHeight + kRowHeight * 2 + kGroupGap;

    // ===== 系统服务 =====
    CreateGroupCaption(body_, "系统服务", y);
    constexpr int kServiceCount = sizeof(kServiceRows) / sizeof(kServiceRows[0]);
    auto* list = CreateGroupList(body_, kGridX, y, kGridWidth, kRowHeight * kServiceCount);
    index = 0;
    for (const auto& spec : kServiceRows) {
        auto* row = CreateListRow(list, index++);
        CreateListIcon(row, spec.icon);
        lv_obj_align(CreateListLabel(row, spec.title), LV_ALIGN_LEFT_MID, kRowTextX, 0);
        CreateRowChevron(row);
        auto& out = services_[static_cast<int>(spec.service)];
        out.label = CreateListLabel(row, "", true);
        lv_obj_align(out.label, LV_ALIGN_RIGHT_MID, -18, 0);
        out.dot = CreateDot(row);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(spec.service)));
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* self = static_cast<SystemInfoApp*>(lv_event_get_user_data(e));
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            self->OpenDetail(static_cast<Service>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target))));
        }, LV_EVENT_CLICKED, this);
    }
    y += kRowHeight * kServiceCount + kGroupGap;

    // ===== 固件 =====
    CreateGroupCaption(body_, "固件", y);
    esp_chip_info_t chip = {};
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    char text[48] = {};
    auto* firmware = CreateGroupList(body_, kGridX, y, kGridWidth, kRowHeight * 6);
    const auto add_fact = [&](int row_index, const char* key, const char* value) {
        auto* row = CreateListRow(firmware, row_index, kGridWidth, kRowHeight, false);
        lv_obj_align(CreateListLabel(row, key), LV_ALIGN_LEFT_MID, 0, 0);
        // Keys are at most four characters (≈64 px), so values get the rest of the 276 px row.
        auto* label = CreateRowValue(row, 200, false);
        lv_obj_set_style_text_font(label, &phone_font_14, 0);
        lv_label_set_text(label, value);
    };
    add_fact(0, "版本", app != nullptr ? app->version : "—");
    if (app != nullptr) {
        FormatBuildTime(app->date, app->time, text, sizeof(text));
        add_fact(1, "构建时间", text);
    } else {
        add_fact(1, "构建时间", "—");
    }
    add_fact(2, "ESP-IDF", esp_get_idf_version());
    std::snprintf(text, sizeof(text), "ESP32-S3 v%u.%u · %u 核",
                  static_cast<unsigned>(chip.revision / 100), static_cast<unsigned>(chip.revision % 100),
                  static_cast<unsigned>(chip.cores));
    add_fact(3, "芯片", text);
    // Chip specification. The 资源 group shows only the heap: with SPIRAM_FETCH_INSTRUCTIONS
    // the firmware code is copied into PSRAM at boot, so the PSRAM heap is smaller than the chip.
    const size_t psram_size = esp_psram_get_size();
    if (psram_size > 0) {
        std::snprintf(text, sizeof(text), "%u MB Flash · %u MB PSRAM",
                      static_cast<unsigned>(flash_size / (1024U * 1024U)),
                      static_cast<unsigned>(psram_size / (1024U * 1024U)));
    } else {
        std::snprintf(text, sizeof(text), "%u MB Flash", static_cast<unsigned>(flash_size / (1024U * 1024U)));
    }
    add_fact(4, "存储", text);
    auto* dev_row = CreateListRow(firmware, 5);
    lv_obj_align(CreateListLabel(dev_row, "开发信息"), LV_ALIGN_LEFT_MID, 0, 0);
    CreateRowChevron(dev_row);
    lv_obj_add_event_cb(dev_row, [](lv_event_t* e) {
        static_cast<SystemInfoApp*>(lv_event_get_user_data(e))->OpenDetail(Service::kDeveloper);
    }, LV_EVENT_CLICKED, this);
}

void SystemInfoApp::Refresh() {
    if (body_ == nullptr) return;
    RefreshResources();
    RefreshNetworkPower();
    RefreshServices();
    RefreshDetail();
}

void SystemInfoApp::RefreshResources() {
    const auto show = [](ResourceRow& row, uint64_t free_bytes, uint64_t total_bytes) {
        if (row.value == nullptr) return;
        char free_text[16] = {};
        char total_text[16] = {};
        FormatBytes(free_bytes, free_text, sizeof(free_text));
        FormatBytes(total_bytes, total_text, sizeof(total_text));
        lv_label_set_text_fmt(row.value, "%s 可用 / %s", free_text, total_text);
        lv_obj_set_style_text_color(row.value, rodakos_theme_text_secondary(), 0);
        const int used = total_bytes > 0
            ? static_cast<int>((total_bytes - free_bytes) * 100 / total_bytes) : 0;
        lv_bar_set_value(row.bar, used, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(row.bar,
            used >= kResourceWarnPercent ? rodakos_theme_warning() : rodakos_theme_primary(),
            LV_PART_INDICATOR);
    };
    show(sram_, heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
         heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    show(psram_, heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
         heap_caps_get_total_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    bool checked = false;
    bool mounted = false;
    rodakos::FileService::Capacity capacity{};
    if (storage_snapshot_) {
        {
            std::lock_guard<std::mutex> lock(storage_snapshot_->mutex);
            checked = storage_snapshot_->checked;
            mounted = storage_snapshot_->mounted;
            capacity = storage_snapshot_->capacity;
        }
        // Free space changes slowly; refresh it every 10 s instead of on every 2 s tick.
        if (esp_timer_get_time() / 1000 - storage_snapshot_->updated_ms > 10000) {
            RequestStorageProbe(false);
        }
    }
    if (mounted && capacity.total_bytes > 0) {
        show(storage_, capacity.free_bytes, capacity.total_bytes);
        return;
    }
    lv_label_set_text(storage_.value, mounted ? "无法读取" : checked ? "未插入" : "正在读取…");
    lv_obj_set_style_text_color(storage_.value, rodakos_theme_text_tertiary(), 0);
    lv_bar_set_value(storage_.bar, 0, LV_ANIM_OFF);
}

void SystemInfoApp::RefreshNetworkPower() {
    auto* wifi = context_->services().wifi();
    if (wifi == nullptr) {
        lv_label_set_text(wifi_name_, "不可用");
        lv_obj_set_style_text_color(wifi_name_, rodakos_theme_text_tertiary(), 0);
        lv_label_set_text(wifi_detail_, "");
    } else if (wifi->GetStatus() == WiFiStatus::kConnected) {
        const std::string ssid = wifi->GetConnectedSSID();
        const std::string ip = wifi->GetIPAddress();
        lv_label_set_text(wifi_name_, ssid.empty() ? "已连接" : ssid.c_str());
        lv_obj_set_style_text_color(wifi_name_, rodakos_theme_text_primary(), 0);
        lv_label_set_text(wifi_detail_, ip.empty() ? "正在获取 IP…" : ip.c_str());
    } else {
        const bool connecting = wifi->GetStatus() == WiFiStatus::kConnecting;
        lv_label_set_text(wifi_name_, connecting ? "正在连接…" : "未连接");
        lv_obj_set_style_text_color(wifi_name_,
            connecting ? rodakos_theme_text_secondary() : rodakos_theme_warning(), 0);
        const std::string rejected = wifi->AuthRejectedSSID();
        const std::string reason = rejected.empty() ? std::string() : rejected + " 密码错误";
        lv_label_set_text(wifi_detail_, reason.c_str());
    }

    rodakos::BatterySnapshot battery;
    if (battery_ != nullptr) battery = battery_->Read();
    if (battery.level_percent < 0) {
        lv_label_set_text(battery_value_, "—");
        lv_obj_set_style_text_color(battery_value_, rodakos_theme_text_tertiary(), 0);
    } else {
        const bool charging = battery.charging_valid && battery.charging;
        if (battery.voltage_mv > 0) {
            lv_label_set_text_fmt(battery_value_, "%s%d%% · %d.%02d V", charging ? "充电中 " : "",
                                  battery.level_percent, battery.voltage_mv / 1000,
                                  battery.voltage_mv % 1000 / 10);
        } else {
            lv_label_set_text_fmt(battery_value_, "%s%d%%", charging ? "充电中 " : "", battery.level_percent);
        }
        lv_obj_set_style_text_color(battery_value_,
            !charging && battery.level_percent < kBatteryLowPercent ? rodakos_theme_warning()
                                                                    : rodakos_theme_text_secondary(),
            0);
    }

    char uptime[32] = {};
    FormatUptime(static_cast<uint64_t>(esp_timer_get_time() / 1000000LL), uptime, sizeof(uptime));
    lv_label_set_text(uptime_value_, uptime);
}

SystemInfoApp::Status SystemInfoApp::ServiceStatus(Service service) {
    switch (service) {
        case Service::kCamera: {
            if (camera_service_ == nullptr) return {"不可用", Tone::kOff};
            const auto state = camera_service_->GetState();
            if (!state.last_error.empty()) return {"出错", Tone::kError};
            if (state.preview_running) return {"使用中", Tone::kActive};
            return {"空闲", Tone::kIdle};
        }
        case Service::kAudio: {
            if (audio_service_ == nullptr) return {"不可用", Tone::kOff};
            switch (audio_service_->GetState().status) {
                case rodakos::AudioPlaybackStatus::kPlaying: return {"播放中", Tone::kActive};
                case rodakos::AudioPlaybackStatus::kLoading: return {"加载中", Tone::kActive};
                case rodakos::AudioPlaybackStatus::kPaused: return {"已暂停", Tone::kIdle};
                case rodakos::AudioPlaybackStatus::kError: return {"出错", Tone::kError};
                default: return {"空闲", Tone::kIdle};
            }
        }
        case Service::kWake: {
            if (voice_wake_service_ == nullptr) return {"不可用", Tone::kOff};
            const auto state = voice_wake_service_->GetState();
            switch (state.status) {
                case rodakos::VoiceWakeStatus::kListening: return {"监听中", Tone::kActive};
                case rodakos::VoiceWakeStatus::kAssistantActive: return {"对话中", Tone::kActive};
                case rodakos::VoiceWakeStatus::kError: return {"出错", Tone::kError};
                case rodakos::VoiceWakeStatus::kUnavailable: return {"不可用", Tone::kOff};
                case rodakos::VoiceWakeStatus::kDisabled:
                default:
                    return state.enabled ? Status{"已暂停", Tone::kIdle} : Status{"已关闭", Tone::kOff};
            }
        }
        case Service::kAssistant: {
            if (voice_assistant_service_ == nullptr) return {"不可用", Tone::kOff};
            switch (voice_assistant_service_->GetState().phase) {
                case rodakos::VoiceAssistantPhase::kConnecting: return {"连接中", Tone::kActive};
                case rodakos::VoiceAssistantPhase::kListening: return {"在听", Tone::kActive};
                case rodakos::VoiceAssistantPhase::kSpeaking: return {"在说", Tone::kActive};
                case rodakos::VoiceAssistantPhase::kError: return {"出错", Tone::kError};
                default: return {"空闲", Tone::kIdle};
            }
        }
        case Service::kMotion: {
            if (motion_service_ == nullptr) return {"不可用", Tone::kOff};
            const auto state = motion_service_->GetState();
            if (!state.last_error.empty()) return {"出错", Tone::kError};
            if (!state.backend_configured) return {"未配置", Tone::kOff};
            return state.active ? Status{"使用中", Tone::kActive} : Status{"空闲", Tone::kIdle};
        }
        case Service::kButtons: {
            if (button_service_ == nullptr || button_service_->ListButtons().empty()) {
                return {"无", Tone::kOff};
            }
            return {nullptr, Tone::kActive};  // Text is the button count; see RefreshServices.
        }
        case Service::kWebFiles: {
            if (web_files_service_ == nullptr) return {"不可用", Tone::kOff};
            const auto state = web_files_service_->GetState();
            if (!state.running) return {"已关闭", Tone::kOff};
            return state.busy ? Status{"上传中", Tone::kActive} : Status{"运行中", Tone::kActive};
        }
        case Service::kDeveloper:
        case Service::kCount:
            break;
    }
    return {};
}

void SystemInfoApp::ApplyStatus(lv_obj_t* dot, lv_obj_t* label, Status status) {
    lv_color_t color = rodakos_theme_text_tertiary();
    lv_color_t text = rodakos_theme_text_secondary();
    switch (status.tone) {
        case Tone::kActive: color = rodakos_theme_success(); break;
        case Tone::kWarning: color = text = rodakos_theme_warning(); break;
        case Tone::kError: color = text = rodakos_theme_error(); break;
        case Tone::kIdle:
        case Tone::kOff: break;
    }
    if (label != nullptr && status.word != nullptr) {
        lv_label_set_text(label, status.word);
        lv_obj_set_style_text_color(label, text, 0);
    }
    if (dot == nullptr) return;
    const bool hollow = status.tone == Tone::kOff;
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, hollow ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dot, color, 0);
    lv_obj_set_style_border_width(dot, hollow ? 1 : 0, 0);
}

void SystemInfoApp::RefreshServices() {
    for (const auto& spec : kServiceRows) {
        auto& row = services_[static_cast<int>(spec.service)];
        if (row.label == nullptr) continue;
        Status status = ServiceStatus(spec.service);
        if (spec.service == Service::kButtons && status.word == nullptr) {
            lv_label_set_text_fmt(row.label, "%u 个",
                                  static_cast<unsigned>(button_service_->ListButtons().size()));
            lv_obj_set_style_text_color(row.label, rodakos_theme_text_secondary(), 0);
        }
        ApplyStatus(row.dot, row.label, status);
        // The dot sits just left of the status word, whose width changes with the text.
        lv_obj_update_layout(row.label);
        lv_obj_align_to(row.dot, row.label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    }
}

void SystemInfoApp::OpenDetail(Service service) {
    if (root_ == nullptr || service == Service::kCount) return;
    CloseDetail();
    detail_service_ = service;
    lv_obj_add_flag(body_, LV_OBJ_FLAG_HIDDEN);
    if (title_label_ != nullptr) lv_label_set_text(title_label_, ServiceTitle(service));

    detail_ = lv_obj_create(root_);
    lv_obj_remove_style_all(detail_);
    lv_obj_set_size(detail_, LV_PCT(100), LV_VER_RES - kRodakosAppHeaderHeight);
    lv_obj_set_pos(detail_, 0, kRodakosAppHeaderHeight);
    lv_obj_clear_flag(detail_, LV_OBJ_FLAG_SCROLLABLE);

    // Summary card: icon, the hardware or component behind the service (the header already
    // names the service), and its state in words.
    const ServiceRowSpec& spec = Spec(service);
    auto* hero = CreateGroupList(detail_, kGridX, 6, kGridWidth, 52);
    auto* icon = CreateListIcon(hero, spec.icon);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 12, 0);
    detail_heading_ = CreateListLabel(hero, spec.hardware != nullptr ? spec.hardware : spec.title);
    lv_obj_set_pos(detail_heading_, 46, 4);
    lv_obj_set_width(detail_heading_, kGridWidth - 58);
    lv_label_set_long_mode(detail_heading_, LV_LABEL_LONG_DOT);
    detail_dot_ = CreateDot(hero);
    lv_obj_set_pos(detail_dot_, 46, 34);
    detail_status_ = CreateListLabel(hero, "", true);
    lv_obj_set_pos(detail_status_, 60, 26);
    lv_obj_set_width(detail_status_, kGridWidth - 72);
    lv_label_set_long_mode(detail_status_, LV_LABEL_LONG_DOT);

    auto* facts = CreateGroupList(detail_, kGridX, 64, kGridWidth, kDetailRowHeight * kDetailRows);
    for (int i = 0; i < kDetailRows; ++i) {
        auto* row = CreateListRow(facts, i, kGridWidth, kDetailRowHeight, false);
        detail_keys_[i] = CreateListLabel(row, "", true);
        lv_obj_align(detail_keys_[i], LV_ALIGN_LEFT_MID, 0, 0);
        detail_values_[i] = CreateRowValue(row, 180, false);
    }

    // Raw fields for comparing with the serial log.
    detail_raw_ = CreateNumberLabel(detail_, &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(detail_raw_, kGridWidth - 4);
    lv_label_set_long_mode(detail_raw_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(detail_raw_, kGridX + 4, 64 + kDetailRowHeight * kDetailRows + 4);

    RefreshDetail();
}

void SystemInfoApp::CloseDetail() {
    if (detail_ != nullptr && lv_obj_is_valid(detail_)) lv_obj_delete(detail_);
    detail_ = nullptr;
    detail_heading_ = nullptr;
    detail_dot_ = nullptr;
    detail_status_ = nullptr;
    for (int i = 0; i < kDetailRows; ++i) {
        detail_keys_[i] = nullptr;
        detail_values_[i] = nullptr;
    }
    detail_raw_ = nullptr;
    detail_service_ = Service::kCount;
    if (body_ != nullptr) lv_obj_clear_flag(body_, LV_OBJ_FLAG_HIDDEN);
    if (title_label_ != nullptr) lv_label_set_text(title_label_, kTitle);
}

void SystemInfoApp::SetDetailRow(int index, const char* key, const char* value, bool error) {
    if (index < 0 || index >= kDetailRows || detail_keys_[index] == nullptr) return;
    lv_label_set_text(detail_keys_[index], key);
    lv_label_set_text(detail_values_[index], value != nullptr && value[0] != '\0' ? value : "—");
    lv_obj_set_style_text_font(detail_values_[index], &phone_font_14, 0);
    lv_obj_set_style_text_color(detail_values_[index],
                                error ? rodakos_theme_error() : rodakos_theme_text_primary(), 0);
}

void SystemInfoApp::RefreshDetail() {
    if (detail_ == nullptr) return;
    char a[48] = {};
    char b[48] = {};
    Status status = ServiceStatus(detail_service_);
    std::string status_text = status.word != nullptr ? status.word : "";
    std::string raw;

    switch (detail_service_) {
        case Service::kCamera: {
            const auto state = camera_service_ != nullptr ? camera_service_->GetState() : rodakos::CameraState{};
            status_text += state.preview_running ? "，正在预览" : "";
            if (state.width > 0) std::snprintf(a, sizeof(a), "%d × %d", state.width, state.height);
            std::snprintf(b, sizeof(b), "%u 帧", static_cast<unsigned>(state.frame_count));
            SetDetailRow(0, "预览", state.preview_running ? "运行中" : "未运行");
            SetDetailRow(1, "分辨率", a);
            SetDetailRow(2, "已采集", b);
            SetDetailRow(3, "最近错误", state.last_error.empty() ? "无" : state.last_error.c_str(),
                         !state.last_error.empty());
            std::snprintf(a, sizeof(a), "available=%d preview=%d frames=%u", state.available ? 1 : 0,
                          state.preview_running ? 1 : 0, static_cast<unsigned>(state.frame_count));
            raw = a;
            break;
        }
        case Service::kAudio: {
            const auto state = audio_service_ != nullptr ? audio_service_->GetState() : rodakos::AudioPlaybackState{};
            const auto focus = audio_focus_service_ != nullptr ? audio_focus_service_->GetState()
                                                               : rodakos::AudioFocusState{};
            SetDetailRow(0, "正在使用", focus.active ? AppName(context_, focus.owner) : "无");
            if (state.sample_rate > 0) {
                std::snprintf(a, sizeof(a), "%u.%u kHz · %s", static_cast<unsigned>(state.sample_rate / 1000),
                              static_cast<unsigned>(state.sample_rate % 1000 / 100),
                              state.channels >= 2 ? "立体声" : "单声道");
            }
            SetDetailRow(1, "格式", a);
            std::snprintf(b, sizeof(b), "%d%%", state.volume);
            SetDetailRow(2, "音量", b);
            std::snprintf(a, sizeof(a), "%u 首", static_cast<unsigned>(
                music_player_service_ != nullptr ? music_player_service_->track_count() : 0));
            SetDetailRow(3, "曲库", a);
            const bool out_open = audio_output_service_ != nullptr && audio_output_service_->IsOpen();
            std::snprintf(a, sizeof(a), "out=%s focus=%s:%s vol=%d", out_open ? "open" : "closed",
                          focus.active ? focus.owner.c_str() : "none", FocusGainText(focus.gain), state.volume);
            raw = a;
            break;
        }
        case Service::kWake: {
            const auto state = voice_wake_service_ != nullptr ? voice_wake_service_->GetState()
                                                              : rodakos::VoiceWakeState{};
            const auto assistant = voice_assistant_service_ != nullptr ? voice_assistant_service_->GetState()
                                                                       : rodakos::VoiceAssistantState{};
            if (state.message == "Paused for music") status_text += "，音乐正在播放";
            if (!state.voice_identity.wake_word.empty()) {
                lv_label_set_text(detail_heading_, state.voice_identity.wake_word.c_str());
            }
            SetDetailRow(0, "唤醒词", state.voice_identity.wake_word.c_str());
            SetDetailRow(1, "识别引擎", state.runtime_name.c_str());
            SetDetailRow(2, "麦克风", assistant.recorder_name.c_str());
            const bool failed = state.status == rodakos::VoiceWakeStatus::kError;
            SetDetailRow(3, "最近错误", failed ? state.message.c_str() : "无", failed);
            raw = "enabled=" + std::string(state.enabled ? "1" : "0") +
                  " listening=" + (state.listening ? "1" : "0") + " msg=" + state.message;
            break;
        }
        case Service::kAssistant: {
            const auto state = voice_assistant_service_ != nullptr ? voice_assistant_service_->GetState()
                                                                   : rodakos::VoiceAssistantState{};
            const char* trigger = state.trigger == rodakos::VoiceAssistantTrigger::kWakeWord ? "唤醒词"
                : state.trigger == rodakos::VoiceAssistantTrigger::kRemote ? "远程" : "手动";
            SetDetailRow(0, "最近触发", trigger);
            SetDetailRow(1, "连接方式", state.transport_name.c_str());
            SetDetailRow(2, "麦克风", state.recorder_name.c_str());
            const bool failed = state.phase == rodakos::VoiceAssistantPhase::kError;
            SetDetailRow(3, "最近错误", failed ? state.message.c_str() : "无", failed);
            raw = "transport=" + std::string(state.transport_active ? "1" : "0") +
                  " recorder=" + (state.recorder_active ? "1" : "0") + " msg=" + state.message;
            break;
        }
        case Service::kMotion: {
            const auto state = motion_service_ != nullptr ? motion_service_->GetState()
                                                          : rodakos::MotionServiceState{};
            if (!state.sensor_name.empty()) lv_label_set_text(detail_heading_, state.sensor_name.c_str());
            SetDetailRow(0, "传感器", state.sensor_name.c_str());
            SetDetailRow(1, "已配置", state.backend_configured ? "是" : "否");
            std::snprintf(a, sizeof(a), "%u 个", static_cast<unsigned>(state.active_clients));
            SetDetailRow(2, "使用中的应用", a);
            SetDetailRow(3, "最近错误", state.last_error.empty() ? "无" : state.last_error.c_str(),
                         !state.last_error.empty());
            std::snprintf(a, sizeof(a), "active=%d clients=%u", state.active ? 1 : 0,
                          static_cast<unsigned>(state.active_clients));
            raw = a;
            break;
        }
        case Service::kButtons: {
            const auto* buttons = button_service_ != nullptr ? &button_service_->ListButtons() : nullptr;
            const size_t count = buttons != nullptr ? buttons->size() : 0;
            std::snprintf(a, sizeof(a), "%u 个", static_cast<unsigned>(count));
            status_text = count > 0 ? "已识别" : "没有检测到按键";
            SetDetailRow(0, "数量", a);
            for (int i = 0; i < 3; ++i) {
                if (buttons != nullptr && static_cast<size_t>(i) < count) {
                    const auto& button = (*buttons)[i];
                    SetDetailRow(i + 1, button.title.empty() ? button.id.c_str() : button.title.c_str(),
                                 button.available ? "可用" : "不可用", !button.available);
                } else {
                    SetDetailRow(i + 1, "", "");
                    lv_label_set_text(detail_values_[i + 1], "");
                }
            }
            for (size_t i = 0; buttons != nullptr && i < count; ++i) {
                if (!raw.empty()) raw += ' ';
                raw += (*buttons)[i].id + "=" + ((*buttons)[i].available ? "1" : "0");
            }
            break;
        }
        case Service::kWebFiles: {
            const auto state = web_files_service_ != nullptr ? web_files_service_->GetState()
                                                             : rodakos::WebFileSystemServiceState{};
            SetDetailRow(0, "地址", state.running ? state.url.c_str() : "—");
            SetDetailRow(1, "访问码", state.running ? state.token.c_str() : "—");
            const size_t slash = state.last_file.find_last_of('/');
            SetDetailRow(2, "最近上传", state.last_file.empty() ? "无"
                         : state.last_file.substr(slash == std::string::npos ? 0 : slash + 1).c_str());
            SetDetailRow(3, "状态信息", state.message.c_str());
            std::snprintf(a, sizeof(a), "running=%d busy=%d", state.running ? 1 : 0, state.busy ? 1 : 0);
            raw = a;
            break;
        }
        case Service::kDeveloper: {
            const auto host = context_->navigation().GetAppHostState();
            status = {"仅供调试", Tone::kIdle};
            status_text = status.word;
            SetDetailRow(0, "前台应用", host.has_current ? host.current_app_id.c_str() : "无");
            std::snprintf(a, sizeof(a), "0x%08x", static_cast<unsigned>(host.current_capabilities));
            SetDetailRow(1, "应用能力位", a);
            SetDetailRow(2, "I2C 总线",
                         esp_board_manager_check_name(ESP_BOARD_PERIPH_NAME_I2C_MASTER) ? "已声明" : "缺失");
            FormatBytes(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), a, sizeof(a));
            FormatBytes(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), b, sizeof(b));
            std::string blocks = std::string(a) + " / " + b;
            SetDetailRow(3, "最大连续块", blocks.c_str());
            raw = std::string("transition=") + (host.transition_in_progress ? "1" : "0") +
                  " sd=" + (StorageMounted() ? "mounted" : "none");
            break;
        }
        case Service::kCount:
            return;
    }

    ApplyStatus(detail_dot_, nullptr, status);
    lv_label_set_text(detail_status_, status_text.c_str());
    lv_obj_set_style_text_color(detail_status_,
        status.tone == Tone::kError ? rodakos_theme_error()
        : status.tone == Tone::kWarning ? rodakos_theme_warning() : rodakos_theme_text_secondary(), 0);
    lv_label_set_text(detail_raw_, raw.c_str());
}

namespace {
struct StorageProbeArgs {
    std::shared_ptr<void> keep_alive;
    void* snapshot = nullptr;
    rodakos::FileService* files = nullptr;
    bool allow_mount = false;
};
}  // namespace

bool SystemInfoApp::StorageMounted() const {
    if (!storage_snapshot_) return false;
    std::lock_guard<std::mutex> lock(storage_snapshot_->mutex);
    return storage_snapshot_->mounted;
}

void SystemInfoApp::RequestStorageProbe(bool allow_mount) {
    if (!storage_snapshot_) return;
    if (file_service_ == nullptr) {
        std::lock_guard<std::mutex> lock(storage_snapshot_->mutex);
        storage_snapshot_->checked = true;
        storage_snapshot_->updated_ms = esp_timer_get_time() / 1000;
        return;
    }
    bool expected = false;
    if (!storage_snapshot_->probing.compare_exchange_strong(expected, true)) return;
    auto* args = new (std::nothrow) StorageProbeArgs{storage_snapshot_, storage_snapshot_.get(),
                                                     file_service_, allow_mount};
    auto task = [](void* arg) {
        auto* probe = static_cast<StorageProbeArgs*>(arg);
        auto* snapshot = static_cast<StorageSnapshot*>(probe->snapshot);
        bool mounted = probe->files->IsMounted();
        if (!mounted && probe->allow_mount) mounted = probe->files->Init();
        rodakos::FileService::Capacity capacity{};
        if (mounted && !probe->files->GetCapacity(capacity)) capacity = {};
        {
            std::lock_guard<std::mutex> lock(snapshot->mutex);
            snapshot->checked = true;
            snapshot->mounted = mounted;
            snapshot->capacity = capacity;
            snapshot->updated_ms = esp_timer_get_time() / 1000;
        }
        snapshot->probing.store(false);
        delete probe;
        vTaskDelete(nullptr);
    };
    if (args == nullptr || xTaskCreate(task, "sysinfo_sd", 4096, args, 2, nullptr) != pdPASS) {
        delete args;
        storage_snapshot_->probing.store(false);
    }
}

bool SystemInfoApp::OnBackRequested() {
    NavigateBack();
    return true;
}

void SystemInfoApp::NavigateBack() {
    // The detail page belongs to this app: close it in place, the list keeps its scroll position.
    if (detail_ != nullptr) {
        lv_async_call([](void* user_data) {
            auto* self = static_cast<SystemInfoApp*>(user_data);
            if (self != nullptr && self->ui_ != nullptr) self->CloseDetail();
        }, this);
        return;
    }
    lv_async_call(DeferReturnToCaller, context_);
}

void SystemInfoApp::NavigateHome() {
    ESP_LOGI(TAG, "Returning home");
    lv_async_call(DeferReturnHome, context_);
}

void RegisterSystemInfoApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "system",
        .title = "System",
        .icon = ICON_INFO,
        .category = PhoneAppCategory::kSystem,
        .capabilities = PhoneCapability::kStorage | PhoneCapability::kNetwork,
        .show_on_home = true,
        .aliases = {"info", "status", "device", "系统", "状态", "关于本机"},
        .create = []() { return std::make_unique<SystemInfoApp>(); },
    });
}
