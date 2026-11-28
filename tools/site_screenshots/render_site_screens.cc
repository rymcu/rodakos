// Renders production HomeApp screens (all 4 firmware themes + page 2 + Shell voice caption
// overlay) on an in-memory LVGL 9.3 RGB565 display for the RodakOS landing page.

#include "phone_os/phone_app.h"

#define private public
#include "apps/home/home_app.h"
#undef private

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_icons.h"
#include "settings.h"

#include <lvgl.h>
#include <src/others/test/lv_test.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

extern "C" const lv_font_t rodakos_icons_20_4;
extern "C" const lv_font_t rodakos_icons_30_4;
extern "C" const lv_font_t font_puhui_16_4;
extern "C" const lv_font_t font_edix_36_4;

lv_font_t phone_font_12;
lv_font_t phone_font_14;
lv_font_t phone_font_18;
lv_font_t phone_font_logo;

namespace {

lv_font_t g_icon_20;

struct AppSpec {
    const char* id;
    const char* title;
    const char* icon;
    PhoneAppCategory category;
    PhoneAppIconTint tint;
};

// Matches main/apps/built_in_apps.cc registration order (all 15 non-Home built-in apps).
constexpr AppSpec kApps[] = {
    {"settings", "Settings", ICON_SETTINGS, PhoneAppCategory::kSystem, PhoneAppIconTint::kDefault},
    {"photos", "Photos", ICON_IMAGE, PhoneAppCategory::kMedia, PhoneAppIconTint::kWarm},
    {"camera", "Camera", ICON_CAMERA, PhoneAppCategory::kMedia, PhoneAppIconTint::kDefault},
    {"clock", "Clock", ICON_CLOCK, PhoneAppCategory::kTools, PhoneAppIconTint::kWarm},
    {"calendar", "Calendar", ICON_CALENDAR, PhoneAppCategory::kTools, PhoneAppIconTint::kDefault},
    {"files", "Files", ICON_FOLDER, PhoneAppCategory::kTools, PhoneAppIconTint::kNeutral},
    {"gyro", "Gyro", ICON_COMPASS, PhoneAppCategory::kTools, PhoneAppIconTint::kDefault},
    {"system", "System", ICON_INFO, PhoneAppCategory::kSystem, PhoneAppIconTint::kDefault},
    {"music", "Music", ICON_MUSIC, PhoneAppCategory::kMedia, PhoneAppIconTint::kDefault},
    {"recorder", "Recorder", ICON_MICROPHONE, PhoneAppCategory::kMedia, PhoneAppIconTint::kDefault},
    {"assistant", "Assistant", ICON_ASSISTANT, PhoneAppCategory::kTools, PhoneAppIconTint::kCool},
    {"smart", "Smart", ICON_LIGHT, PhoneAppCategory::kTools, PhoneAppIconTint::kCool},
    {"wol", "Wake", ICON_POWER, PhoneAppCategory::kTools, PhoneAppIconTint::kAlert},
    {"themes", "Themes", ICON_PALETTE, PhoneAppCategory::kSystem, PhoneAppIconTint::kDefault},
    {"messages", "Messages", ICON_NOTIFICATIONS, PhoneAppCategory::kTools, PhoneAppIconTint::kAlert},
};

void Pump(uint32_t ms = 30) {
    lv_test_wait(ms);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_update_layout(lv_layer_top());
    lv_refr_now(nullptr);
}

void WriteRgb565Ppm(const std::string& path) {
    const lv_draw_buf_t* buffer = lv_display_get_buf_active(lv_display_get_default());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n320 240\n255\n";
    std::vector<uint8_t> row(320 * 3);
    for (int32_t y = 0; y < 240; ++y) {
        const auto* line = reinterpret_cast<const uint16_t*>(
            buffer->data + static_cast<size_t>(y) * buffer->header.stride);
        for (int32_t x = 0; x < 320; ++x) {
            const uint16_t pixel = line[x];
            const uint8_t r5 = (pixel >> 11) & 0x1F;
            const uint8_t g6 = (pixel >> 5) & 0x3F;
            const uint8_t b5 = pixel & 0x1F;
            row[static_cast<size_t>(x) * 3 + 0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
            row[static_cast<size_t>(x) * 3 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
            row[static_cast<size_t>(x) * 3 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
}

struct HomeFixture {
    explicit HomeFixture(const char* theme)
        : ui(320, 240), context(ui, navigation, registry, services, settings) {
        rodakos_theme_init_from_name(theme);
        ui.SyncThemeName(theme);
        for (const auto& spec : kApps) {
            PhoneAppDescriptor descriptor;
            descriptor.id = spec.id;
            descriptor.title = spec.title;
            descriptor.icon = spec.icon;
            descriptor.category = spec.category;
            descriptor.icon_tint = spec.tint;
            descriptor.create = []() -> std::unique_ptr<PhoneApp> { return nullptr; };
            registry.Register(std::move(descriptor));
        }
        if (!home.OnCreate(context)) {
            std::fprintf(stderr, "HomeApp::OnCreate failed\n");
            std::exit(1);
        }
        Pump();
        lv_tileview_set_tile(home.tileview_, home.page_tiles_[0], LV_ANIM_OFF);
        lv_obj_send_event(home.tileview_, LV_EVENT_SCROLL_END, nullptr);
        if (home.clock_label_ != nullptr) {
            lv_label_set_text(home.clock_label_, "10:08");
        }
        if (home.wifi_label_ != nullptr) {
            lv_label_set_text(home.wifi_label_, ICON_WIFI);
            lv_obj_set_style_text_color(home.wifi_label_, rodakos_theme_text_primary(), 0);
        }
        if (home.battery_icon_ != nullptr) {
            lv_label_set_text(home.battery_icon_, ICON_BATTERY_FULL);
            lv_obj_set_style_text_color(home.battery_icon_, rodakos_theme_success(), 0);
        }
        if (home.battery_label_ != nullptr) {
            lv_label_set_text(home.battery_label_, "92%");
        }
        if (home.inbox_dot_ != nullptr) {
            lv_obj_clear_flag(home.inbox_dot_, LV_OBJ_FLAG_HIDDEN);
        }
        Pump();
    }

    ~HomeFixture() {
        home.OnDestroy();
        lv_obj_clean(lv_layer_top());
        lv_obj_clean(lv_screen_active());
        Pump();
    }

    PhoneUi ui;
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context;
    HomeApp home;
};

// Reproduces PhoneShell::CreateVoiceOverlay + StyleVoiceOverlay geometry and styling on lv_layer_top().
lv_obj_t* CreateVoiceCaptionPreview(const char* text) {
    lv_obj_t* bar = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, 296);
    lv_obj_set_height(bar, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(bar, 12, 0);
    lv_obj_set_style_pad_ver(bar, 7, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(bar, 18, 0);
    lv_obj_set_style_shadow_offset_y(bar, 4, 0);
    lv_obj_set_style_shadow_spread(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(bar);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    lv_obj_set_style_text_line_space(label, 1, 0);
    lv_obj_set_style_max_height(label, 40, 0);
    lv_label_set_text(label, text);

    const lv_color_t base = rodakos_theme_bg_secondary();
    const bool light = lv_color_luminance(base) > 128;
    const lv_color_t sheen = lv_color_mix(lv_color_white(), base, light ? LV_OPA_60 : LV_OPA_20);
    lv_obj_set_style_radius(bar, 16, 0);
    lv_obj_set_style_bg_color(bar, sheen, 0);
    lv_obj_set_style_bg_grad_color(bar, base, 0);
    lv_obj_set_style_bg_main_opa(bar, light ? LV_OPA_80 : LV_OPA_70, 0);
    lv_obj_set_style_bg_grad_opa(bar, light ? LV_OPA_70 : LV_OPA_80, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, lv_color_white(), 0);
    lv_obj_set_style_border_opa(bar, light ? LV_OPA_70 : LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(bar, light ? LV_OPA_20 : LV_OPA_40, 0);
    lv_obj_set_style_text_color(label, rodakos_theme_text_primary(), 0);

    lv_obj_update_layout(bar);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -8);
    return bar;
}

}  // namespace

void PhoneFontsInit(void) {
    g_icon_20 = rodakos_icons_20_4;
    g_icon_20.fallback = &font_puhui_16_4;
    phone_font_12 = lv_font_montserrat_12;
    phone_font_12.fallback = &g_icon_20;
    phone_font_14 = lv_font_montserrat_14;
    phone_font_14.fallback = &g_icon_20;
    phone_font_18 = lv_font_montserrat_18;
    phone_font_18.fallback = &g_icon_20;
    phone_font_logo = font_edix_36_4;
}

const lv_font_t* PhoneIconFont(void) {
    return &g_icon_20;
}

const lv_font_t* PhoneIconFontLarge(void) {
    return &rodakos_icons_30_4;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
        return 2;
    }
    const std::string out_dir = argv[1];

    lv_init();
    lv_test_display_create(320, 240);
    lv_display_set_color_format(lv_display_get_default(), LV_COLOR_FORMAT_RGB565);
    lv_test_indev_create_all();
    PhoneFontsInit();

    for (const char* theme : {"dark", "light", "blue", "green"}) {
        HomeFixture f(theme);
        WriteRgb565Ppm(out_dir + "/home-" + theme + ".ppm");

        if (std::strcmp(theme, "dark") == 0) {
            // Page 2 shows Wake, Themes, Messages.
            if (f.home.page_tiles_.size() > 1) {
                lv_tileview_set_tile(f.home.tileview_, f.home.page_tiles_[1], LV_ANIM_OFF);
                lv_obj_send_event(f.home.tileview_, LV_EVENT_SCROLL_END, nullptr);
                Pump(50);
                WriteRgb565Ppm(out_dir + "/home-page2-dark.ppm");
                lv_tileview_set_tile(f.home.tileview_, f.home.page_tiles_[0], LV_ANIM_OFF);
                lv_obj_send_event(f.home.tileview_, LV_EVENT_SCROLL_END, nullptr);
                Pump(50);
            }
            // Shell bottom voice caption bar over Home (dark).
            CreateVoiceCaptionPreview("已打开客厅主灯，亮度 60%");
            Pump(40);
            WriteRgb565Ppm(out_dir + "/home-voice-dark.ppm");
        } else if (std::strcmp(theme, "light") == 0) {
            CreateVoiceCaptionPreview("已打开客厅主灯，亮度 60%");
            Pump(40);
            WriteRgb565Ppm(out_dir + "/home-voice-light.ppm");
        }
    }

    lv_deinit();
    std::printf("Rendered RodakOS site Home & Shell screenshots to %s\n", out_dir.c_str());
    return 0;
}
