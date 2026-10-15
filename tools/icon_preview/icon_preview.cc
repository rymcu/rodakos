// Renders the production Home desktop on the host with candidate icon fonts.
//
// The program compiles the real HomeApp, theme, layout and production font sources against an
// in-memory LVGL 9.3 display in RGB565, the device color format. It writes one 320x240 PPM per
// candidate font, theme and state, plus an enlarged tile sheet, into the output directory.
// Only the desktop icon glyph font changes between candidates; everything else is production.

#include "phone_os/phone_app.h"

// The preview reads HomeApp's tile objects, as tests/home_ui does.
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
#include "settings.h"

#include <lvgl.h>
#include <src/others/test/lv_test.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define X(name) extern "C" const lv_font_t name;
#include "candidate_fonts.inc"
#undef X
extern "C" const lv_font_t rodakos_icons_30_4;

namespace {

struct CandidateFont {
    std::string key;
    const lv_font_t* font;
};

const lv_font_t* g_desktop_font = nullptr;

std::vector<CandidateFont> CandidateFonts() {
    std::vector<CandidateFont> fonts = {{"current", &rodakos_icons_30_4}};
#define X(name) fonts.push_back({std::string(#name).substr(5, std::string(#name).size() - 8), &name});
#include "candidate_fonts.inc"
#undef X
    return fonts;
}

struct AppSpec {
    const char* id;
    const char* title;
    PhoneAppCategory category;
    PhoneAppIconTint tint;
};

// Production Registry order (built_in_apps.cc) with production categories and tints.
constexpr AppSpec kApps[] = {
    {"settings", "Settings", PhoneAppCategory::kSystem, PhoneAppIconTint::kDefault},
    {"photos", "Photos", PhoneAppCategory::kMedia, PhoneAppIconTint::kWarm},
    {"camera", "Camera", PhoneAppCategory::kMedia, PhoneAppIconTint::kDefault},
    {"clock", "Clock", PhoneAppCategory::kTools, PhoneAppIconTint::kWarm},
    {"calendar", "Calendar", PhoneAppCategory::kTools, PhoneAppIconTint::kDefault},
    {"files", "Files", PhoneAppCategory::kTools, PhoneAppIconTint::kNeutral},
    {"gyro", "Gyro", PhoneAppCategory::kTools, PhoneAppIconTint::kDefault},
    {"system", "System", PhoneAppCategory::kSystem, PhoneAppIconTint::kDefault},
    {"music", "Music", PhoneAppCategory::kMedia, PhoneAppIconTint::kDefault},
    {"recorder", "Recorder", PhoneAppCategory::kMedia, PhoneAppIconTint::kDefault},
    {"assistant", "Assistant", PhoneAppCategory::kTools, PhoneAppIconTint::kCool},
    {"smart", "Smart", PhoneAppCategory::kTools, PhoneAppIconTint::kCool},
    {"wol", "Wake", PhoneAppCategory::kTools, PhoneAppIconTint::kAlert},
};

// Icon fonts use the BMP private-use area (U+E000..) or, for Iconify builds, plane 15.
std::string Utf8(uint32_t codepoint) {
    std::string out;
    if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return out;
}

// Minimal parser for the "glyphs" object of one set in generated/glyphs.json, which holds hex
// codepoints for every set. Matches the set key as a whole JSON key, so "carbon" never matches
// "carbon_mixed".
std::map<std::string, std::string> LoadGlyphs(const std::string& json, const std::string& key) {
    std::map<std::string, std::string> glyphs;
    const size_t set_at = json.find("\"" + key + "\":", json.find("\"sets\""));
    if (set_at == std::string::npos) return glyphs;
    const size_t open = json.find('{', json.find("\"glyphs\"", set_at));
    const size_t close = json.find('}', open);
    const std::string body = json.substr(open + 1, close - open - 1);
    size_t at = 0;
    while ((at = body.find('"', at)) != std::string::npos) {
        const size_t name_end = body.find('"', at + 1);
        const size_t value_start = body.find('"', name_end + 1);
        const size_t value_end = body.find('"', value_start + 1);
        const std::string name = body.substr(at + 1, name_end - at - 1);
        const uint32_t codepoint = std::strtoul(
            body.substr(value_start + 1, value_end - value_start - 1).c_str(), nullptr, 16);
        glyphs[name] = Utf8(codepoint);
        at = value_end + 1;
    }
    return glyphs;
}

void SwitchToRgb565() {
    lv_display_t* display = lv_display_get_default();
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
}

void Pump(uint32_t milliseconds = 20) {
    lv_test_wait(milliseconds);
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(nullptr);
}

// Expands the RGB565 framebuffer the way the ST7789 receives it (no extra dithering).
void WriteFramebuffer(const std::string& path, int32_t x1, int32_t y1, int32_t w, int32_t h,
                      int scale) {
    const lv_draw_buf_t* buffer = lv_display_get_buf_active(lv_display_get_default());
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w * scale << " " << h * scale << "\n255\n";
    std::vector<uint8_t> row(static_cast<size_t>(w * scale) * 3);
    for (int32_t y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const uint16_t*>(
            buffer->data + static_cast<size_t>(y1 + y) * buffer->header.stride);
        for (int32_t x = 0; x < w; ++x) {
            const uint16_t pixel = line[x1 + x];
            const uint8_t r5 = (pixel >> 11) & 0x1F;
            const uint8_t g6 = (pixel >> 5) & 0x3F;
            const uint8_t b5 = pixel & 0x1F;
            // Replicate the high bits so full-scale RGB565 maps to 255, as the panel shows it.
            const uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
            const uint8_t g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
            const uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
            for (int sx = 0; sx < scale; ++sx) {
                uint8_t* p = &row[static_cast<size_t>(x * scale + sx) * 3];
                p[0] = r;
                p[1] = g;
                p[2] = b;
            }
        }
        for (int sy = 0; sy < scale; ++sy) {
            out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
        }
    }
}

struct HomeRun {
    explicit HomeRun(const std::map<std::string, std::string>& glyphs)
        : ui(320, 240), context(ui, navigation, registry, services, settings) {
        for (const auto& spec : kApps) {
            PhoneAppDescriptor descriptor;
            descriptor.id = spec.id;
            descriptor.title = spec.title;
            const auto glyph = glyphs.find(spec.id);
            descriptor.icon = glyph != glyphs.end() ? glyph->second : "?";
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
        Pump();
    }

    ~HomeRun() {
        lv_test_mouse_release();
        Pump();
        home.OnDestroy();
        Pump();
        lv_obj_clean(lv_screen_active());
        Pump();
    }

    lv_obj_t* Tile(size_t index) {
        lv_obj_t* grid = lv_obj_get_child(home.page_tiles_[0], 0);
        return lv_obj_get_child(grid, static_cast<int32_t>(index));
    }

    PhoneUi ui;
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context;
    HomeApp home;
};

void RenderSet(const std::string& out_dir, const CandidateFont& candidate,
               const std::map<std::string, std::string>& glyphs) {
    g_desktop_font = candidate.font;
    for (const char* theme : {"dark", "light", "blue", "green"}) {
        rodakos_theme_init_from_name(theme);
        HomeRun run(glyphs);
        run.ui.SyncThemeName(theme);
        const std::string base = out_dir + "/" + candidate.key + "_" + theme;
        WriteFramebuffer(base + "_home.ppm", 0, 0, 320, 240, 1);
        // The first eight tiles at 4x, as the eye sees the 2.8-inch panel up close.
        lv_area_t first;
        lv_area_t last;
        lv_obj_get_coords(run.Tile(0), &first);
        lv_obj_get_coords(run.Tile(7), &last);
        WriteFramebuffer(base + "_tiles.ppm", first.x1, first.y1,
                         last.x2 - first.x1 + 1, last.y2 - first.y1 + 1, 4);

        if (std::string_view(theme) == "dark") {
            // One 3x crop per app for the app-by-font matrix. Registry order is the default
            // layout order, so tile i on page 1 is kApps[i].
            const auto write_cell = [&](size_t tile_index, const char* app_id) {
                lv_area_t cell;
                lv_obj_get_coords(run.Tile(tile_index), &cell);
                WriteFramebuffer(out_dir + "/" + candidate.key + "_app_" + app_id + ".ppm",
                                 cell.x1, cell.y1, cell.x2 - cell.x1 + 1, cell.y2 - cell.y1 + 1, 3);
            };
            for (size_t index = 0; index < 12; ++index) {
                write_cell(index, kApps[index].id);
            }

            lv_area_t tile;
            lv_obj_get_coords(run.Tile(8), &tile);
            lv_test_mouse_move_to((tile.x1 + tile.x2) / 2, tile.y1 + 20);
            lv_test_mouse_press();
            Pump(60);
            WriteFramebuffer(base + "_pressed.ppm", tile.x1, tile.y1, tile.x2 - tile.x1 + 1,
                             tile.y2 - tile.y1 + 1, 6);
            lv_test_mouse_release();
            Pump(60);

            // The 13th app (Wake) is alone on page 2.
            lv_tileview_set_tile(run.home.tileview_, run.home.page_tiles_[1], LV_ANIM_OFF);
            lv_obj_send_event(run.home.tileview_, LV_EVENT_SCROLL_END, nullptr);
            Pump(60);
            WriteFramebuffer(base + "_page2.ppm", 0, 0, 320, 240, 1);
            lv_obj_t* page2_grid = lv_obj_get_child(run.home.page_tiles_[1], 0);
            lv_area_t cell;
            lv_obj_get_coords(lv_obj_get_child(page2_grid, 0), &cell);
            WriteFramebuffer(out_dir + "/" + candidate.key + "_app_" + kApps[12].id + ".ppm",
                             cell.x1, cell.y1, cell.x2 - cell.x1 + 1, cell.y2 - cell.y1 + 1, 3);
        }
    }
}

}  // namespace

// Mirrors production phone_fonts.cc, which is not linked, except that PhoneIconFontLarge()
// returns the candidate desktop glyph font.
lv_font_t phone_font_12;
lv_font_t phone_font_14;
lv_font_t phone_font_18;
lv_font_t phone_font_logo;
extern "C" const lv_font_t rodakos_icons_20_4;
extern "C" const lv_font_t font_puhui_16_4;
extern "C" const lv_font_t font_edix_36_4;

namespace {
lv_font_t g_icon_20;
}

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
    return g_desktop_font != nullptr ? g_desktop_font : &rodakos_icons_30_4;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <generated/glyphs.json> <output-dir>\n", argv[0]);
        return 2;
    }
    std::ifstream config_file(argv[1]);
    std::stringstream config;
    config << config_file.rdbuf();
    const std::string json = config.str();

    lv_init();
    lv_test_display_create(320, 240);
    SwitchToRgb565();
    lv_test_indev_create_all();
    PhoneFontsInit();

    for (const auto& candidate : CandidateFonts()) {
        const auto glyphs = LoadGlyphs(json, candidate.key);
        if (glyphs.empty()) {
            std::fprintf(stderr, "No glyphs for %s in %s\n", candidate.key.c_str(), argv[1]);
            return 1;
        }
        RenderSet(argv[2], candidate, glyphs);
        std::printf("%s: rendered\n", candidate.key.c_str());
    }
    lv_deinit();
    return 0;
}
