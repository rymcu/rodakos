#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rodakos {

inline constexpr size_t kAppearanceMaxPackageBytes = 512 * 1024;
inline constexpr size_t kAppearanceMaxDecodedBytes = 384 * 1024;
inline constexpr size_t kAppearanceMaxJsonAllocationBytes = 96 * 1024;
inline constexpr size_t kAppearanceMaxMetadataBytes = 16 * 1024;
inline constexpr uint32_t kAppearanceMaxDurationMs = 5000;
inline constexpr uint16_t kAppearanceWidth = 320;
inline constexpr uint16_t kAppearanceHeight = 240;

enum class AppearancePixelFormat { kA4, kRgb565, kRgb565A8 };
struct AppearanceResource {
    std::string id;
    AppearancePixelFormat format = AppearancePixelFormat::kA4;
    uint16_t width = 0;
    uint16_t height = 0;
    size_t offset = 0;
    size_t length = 0;
};
struct AppearanceAnimationUnit {
    std::string resource_id;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t start_ms = 0;
    uint32_t duration_ms = 0;
};
struct AppearancePackageMetadata {
    std::string animation_kind;
    std::string animation_template;
    uint32_t duration_ms = 2500;
    uint32_t background = 0;
    uint32_t color = 0;
    std::vector<AppearanceAnimationUnit> units;
    std::string wallpaper_resource_id;
    std::string theme_preset;
    uint32_t theme_primary = 0;
    std::vector<AppearanceResource> resources;
    const AppearanceResource* FindResource(const std::string& id) const;
};

bool DecodeAppearancePackage(const uint8_t* bytes, size_t size,
                             AppearancePackageMetadata& metadata,
                             size_t& payload_offset, std::string& error);
bool DecodeAppearanceMetadata(const uint8_t* header, const char* json, size_t json_size,
                              size_t package_size, AppearancePackageMetadata& metadata,
                              std::string& error);
bool IsAppearancePreset(const std::string& preset);
bool IsAppearanceIdentifier(const std::string& value);

}  // namespace rodakos
