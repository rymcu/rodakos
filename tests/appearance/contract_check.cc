#include "rodak_appearance.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string Quote(const std::string& value) {
    std::ostringstream result;
    result << '"';
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') result << '\\' << static_cast<char>(ch);
        else if (ch < 0x20) result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(ch);
        else result << static_cast<char>(ch);
    }
    result << '"';
    return result.str();
}
void Require(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}
void Check(const std::string& path, bool e2e) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(file.is_open(), "file_unavailable");
    const auto length = file.tellg();
    Require(length >= 16 && length <= static_cast<std::streamoff>(rodakos::kAppearanceMaxPackageBytes), "file_size_invalid");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    Require(static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "file_read_failed");
    rodakos::AppearancePackageMetadata metadata;
    size_t payload_offset = 0;
    std::string error;
    if (!rodakos::DecodeAppearancePackage(bytes.data(), bytes.size(), metadata, payload_offset, error)) throw std::runtime_error(error);
    const std::string name = std::filesystem::path(path).filename().string();
    const auto* wallpaper = metadata.FindResource(metadata.wallpaper_resource_id);
    size_t a4_resources = 0, decoded_bytes = 0;
    for (const auto& resource : metadata.resources) {
        if (resource.format == rodakos::AppearancePixelFormat::kA4) {
            ++a4_resources;
            decoded_bytes += static_cast<size_t>(resource.width) * resource.height;
        } else decoded_bytes += resource.length;
    }
    if (e2e) {
        Require(metadata.animation_kind == "text", "expected_text_animation");
        Require(metadata.animation_template == "letters", "expected_letters_template");
        Require(metadata.units.size() == 7, "expected_seven_RODAKOS_units");
        Require(metadata.duration_ms == 2500, "expected_2500ms_animation");
        Require(a4_resources == 7, "expected_seven_A4_glyph_resources");
        for (const auto& unit : metadata.units) {
            const auto* resource = metadata.FindResource(unit.resource_id);
            Require(resource != nullptr && resource->format == rodakos::AppearancePixelFormat::kA4, "glyph_reference_format_mismatch");
        }
        if (name == "edix.rap") {
            Require(metadata.resources.size() == 7 && metadata.wallpaper_resource_id.empty(), "unexpected_edix_wallpaper");
            Require(metadata.theme_preset == "dark" && metadata.theme_primary == 0x79cbff, "edix_default_theme_mismatch");
        } else {
            constexpr char kSuffix[] = "-wallpaper.rap";
            Require(name.size() > sizeof(kSuffix) - 1 && name.substr(name.size() - sizeof(kSuffix) + 1) == kSuffix, "unknown_e2e_fixture_filename");
            const std::string preset = name.substr(0, name.size() - sizeof(kSuffix) + 1);
            Require(rodakos::IsAppearancePreset(preset) && metadata.theme_preset == preset, "wallpaper_theme_preset_mismatch");
            Require(metadata.theme_primary == 0xff00ff, "wallpaper_theme_primary_mismatch");
            Require(metadata.resources.size() == 8 && wallpaper != nullptr, "expected_wallpaper_resource");
            Require(wallpaper->format == rodakos::AppearancePixelFormat::kRgb565 && wallpaper->width == 320 && wallpaper->height == 240 && wallpaper->length == 153600,
                    "wallpaper_RGB565_320x240_153600bytes_mismatch");
        }
    }
    std::cout << "{\"file\":" << Quote(path) << ",\"status\":\"passed\",\"bytes\":" << bytes.size()
              << ",\"payloadBytes\":" << bytes.size() - payload_offset << ",\"animationKind\":" << Quote(metadata.animation_kind)
              << ",\"template\":" << Quote(metadata.animation_template) << ",\"units\":" << metadata.units.size()
              << ",\"durationMs\":" << metadata.duration_ms << ",\"resources\":" << metadata.resources.size()
              << ",\"a4Resources\":" << a4_resources << ",\"themePreset\":" << Quote(metadata.theme_preset)
              << ",\"themePrimary\":" << metadata.theme_primary << ",\"wallpaperBytes\":" << (wallpaper != nullptr ? wallpaper->length : 0)
              << ",\"wallpaperFormat\":" << Quote(wallpaper != nullptr ? "rgb565" : "none")
              << ",\"decodedBytes\":" << decoded_bytes << "}\n";
}
}  // namespace
int main(int argc, char** argv) {
    bool e2e = false;
    int first = 1;
    if (argc > 1 && std::string(argv[1]) == "--e2e") { e2e = true; first = 2; }
    if (argc <= first) {
        std::cerr << "Usage: appearance_contract_check [--e2e] package.rap [package.rap ...]\n";
        return 2;
    }
    unsigned failed = 0;
    for (int i = first; i < argc; ++i) {
        try { Check(argv[i], e2e); }
        catch (const std::exception& error) {
            ++failed;
            std::cout << "{\"file\":" << Quote(argv[i]) << ",\"status\":\"failed\",\"error\":" << Quote(error.what()) << "}\n";
        }
    }
    std::cout << "{\"checked\":" << argc - first << ",\"failed\":" << failed << "}\n";
    return failed == 0 ? 0 : 1;
}
