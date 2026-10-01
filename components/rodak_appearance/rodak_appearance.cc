#include "rodak_appearance.h"

#include "rodak_appearance_json.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace rodakos {
namespace {
uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
bool Utf8(const char* text, size_t size) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(text);
    for (size_t i = 0; i < size;) {
        const uint8_t first = bytes[i];
        uint32_t codepoint = 0; size_t count = 0;
        if (first < 0x80) { codepoint = first; count = 1; }
        else if (first >= 0xc2 && first <= 0xdf) { codepoint = first & 0x1f; count = 2; }
        else if (first >= 0xe0 && first <= 0xef) { codepoint = first & 0xf; count = 3; }
        else if (first >= 0xf0 && first <= 0xf4) { codepoint = first & 7; count = 4; }
        else return false;
        if (count > size - i) return false;
        for (size_t j = 1; j < count; ++j) {
            if ((bytes[i + j] & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (bytes[i + j] & 0x3f);
        }
        if ((count == 3 && codepoint < 0x800) || (count == 4 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        i += count;
    }
    return true;
}
const cJSON* Item(const cJSON* object, const char* name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}
bool Number(const cJSON* object, const char* name, int64_t min, int64_t max, int64_t& out) {
    const cJSON* value = Item(object, name);
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        std::floor(value->valuedouble) != value->valuedouble ||
        value->valuedouble < static_cast<double>(min) || value->valuedouble > static_cast<double>(max)) return false;
    out = static_cast<int64_t>(value->valuedouble);
    return true;
}
bool Text(const cJSON* object, const char* name, std::string& out) {
    const cJSON* value = Item(object, name);
    if (!cJSON_IsString(value) || value->valuestring == nullptr) return false;
    out = value->valuestring;
    return true;
}
bool UniqueKeys(const cJSON* object) {
    if (cJSON_IsObject(object)) {
        std::set<std::string> seen;
        for (const cJSON* child = object->child; child != nullptr; child = child->next) {
            if (child->string == nullptr || !seen.insert(child->string).second || !UniqueKeys(child)) return false;
        }
    } else if (cJSON_IsArray(object)) {
        for (const cJSON* child = object->child; child != nullptr; child = child->next) if (!UniqueKeys(child)) return false;
    }
    return true;
}
bool Decode(const cJSON* root, uint16_t resource_count, size_t payload_size,
            AppearancePackageMetadata& out, std::string& error) {
    std::string schema;
    int64_t value = 0;
    const cJSON* animation = Item(root, "animation");
    const cJSON* theme = Item(root, "theme");
    const cJSON* resources = Item(root, "resources");
    if (!cJSON_IsObject(root) || !UniqueKeys(root) || !Text(root, "schema", schema) || schema != "rodak-appearance-v1" ||
        !Number(root, "width", 320, 320, value) || !Number(root, "height", 240, 240, value) ||
        !cJSON_IsObject(animation) || !cJSON_IsObject(theme) || !cJSON_IsArray(resources) ||
        cJSON_GetArraySize(resources) != resource_count || resource_count > 33) {
        error = "package_metadata_invalid"; return false;
    }
    if (!Text(animation, "kind", out.animation_kind) ||
        (out.animation_kind != "text" && out.animation_kind != "image" && out.animation_kind != "builtin") ||
        !Text(animation, "template", out.animation_template) ||
        (out.animation_template != "letters" && out.animation_template != "fade" && out.animation_template != "rise") ||
        !Number(animation, "durationMs", 1000, kAppearanceMaxDurationMs, value)) {
        error = "package_animation_invalid"; return false;
    }
    out.duration_ms = static_cast<uint32_t>(value);
    if (!Number(animation, "background", 0, 0xffffff, value)) { error = "package_background_invalid"; return false; }
    out.background = static_cast<uint32_t>(value);
    if (!Number(animation, "color", 0, 0xffffff, value)) { error = "package_color_invalid"; return false; }
    out.color = static_cast<uint32_t>(value);
    if (!Text(theme, "preset", out.theme_preset) || !IsAppearancePreset(out.theme_preset) ||
        !Number(theme, "primary", 0, 0xffffff, value)) { error = "package_theme_invalid"; return false; }
    out.theme_primary = static_cast<uint32_t>(value);
    std::set<std::string> ids;
    size_t decoded_bytes = 0;
    for (const cJSON* item = resources->child; item != nullptr; item = item->next) {
        AppearanceResource resource;
        std::string format;
        if (!cJSON_IsObject(item) || !Text(item, "id", resource.id) || !IsAppearanceIdentifier(resource.id) ||
            !ids.insert(resource.id).second || !Text(item, "format", format) ||
            !Number(item, "width", 1, 320, value)) { error = "package_resource_invalid"; return false; }
        resource.width = static_cast<uint16_t>(value);
        if (!Number(item, "height", 1, 240, value)) { error = "package_resource_invalid"; return false; }
        resource.height = static_cast<uint16_t>(value);
        if (!Number(item, "offset", 0, kAppearanceMaxPackageBytes, value)) { error = "package_resource_invalid"; return false; }
        resource.offset = static_cast<size_t>(value);
        if (!Number(item, "length", 1, kAppearanceMaxPackageBytes, value)) { error = "package_resource_invalid"; return false; }
        resource.length = static_cast<size_t>(value);
        const size_t pixels = static_cast<size_t>(resource.width) * resource.height;
        size_t expected = 0;
        if (format == "a4") { resource.format = AppearancePixelFormat::kA4; expected = ((resource.width + 1u) / 2u) * resource.height; decoded_bytes += pixels; }
        else if (format == "rgb565") { resource.format = AppearancePixelFormat::kRgb565; expected = pixels * 2; decoded_bytes += expected; }
        else if (format == "rgb565a8") { resource.format = AppearancePixelFormat::kRgb565A8; expected = pixels * 3; decoded_bytes += expected; }
        else { error = "package_format_unsupported"; return false; }
        if (resource.length != expected || resource.offset > payload_size || resource.length > payload_size - resource.offset ||
            decoded_bytes > kAppearanceMaxDecodedBytes) { error = "package_resource_bounds"; return false; }
        out.resources.push_back(std::move(resource));
    }
    size_t end = 0;
    for (const auto& resource : out.resources) {
        if (resource.offset != end) { error = "package_resource_overlap_or_gap"; return false; }
        end += resource.length;
    }
    if (end != payload_size) { error = "package_payload_unindexed"; return false; }
    const cJSON* units = Item(animation, "units");
    if (!cJSON_IsArray(units) || cJSON_GetArraySize(units) > 32 ||
        (out.animation_kind == "builtin" && cJSON_GetArraySize(units) != 0) ||
        (out.animation_kind != "builtin" && cJSON_GetArraySize(units) == 0)) { error = "package_units_invalid"; return false; }
    for (const cJSON* item = units->child; item != nullptr; item = item->next) {
        AppearanceAnimationUnit unit;
        if (!cJSON_IsObject(item) || !Text(item, "resourceId", unit.resource_id) || out.FindResource(unit.resource_id) == nullptr) {
            error = "package_unit_invalid"; return false;
        }
        const auto* resource = out.FindResource(unit.resource_id);
        if (!Number(item, "x", 0, 320 - resource->width, value) ||
            (out.animation_kind == "text" && resource->format != AppearancePixelFormat::kA4)) { error = "package_unit_invalid"; return false; }
        unit.x = static_cast<int32_t>(value);
        if (!Number(item, "y", 0, 240 - resource->height, value)) { error = "package_unit_invalid"; return false; }
        unit.y = static_cast<int32_t>(value);
        if (!Number(item, "startMs", 0, out.duration_ms, value)) { error = "package_unit_invalid"; return false; }
        unit.start_ms = static_cast<uint32_t>(value);
        if (!Number(item, "durationMs", 1, out.duration_ms, value)) { error = "package_unit_invalid"; return false; }
        unit.duration_ms = static_cast<uint32_t>(value);
        if (unit.start_ms + unit.duration_ms > out.duration_ms - 220) { error = "package_unit_timing"; return false; }
        out.units.push_back(std::move(unit));
    }
    if (out.animation_kind == "image" && (out.units.size() != 1 ||
        out.FindResource(out.units[0].resource_id)->format == AppearancePixelFormat::kA4)) {
        error = "package_image_invalid"; return false;
    }
    const cJSON* wallpaper = Item(root, "wallpaperResourceId");
    if (wallpaper != nullptr) {
        if (!Text(root, "wallpaperResourceId", out.wallpaper_resource_id)) { error = "package_wallpaper_invalid"; return false; }
        const auto* resource = out.FindResource(out.wallpaper_resource_id);
        if (resource == nullptr || resource->format != AppearancePixelFormat::kRgb565 || resource->width != 320 || resource->height != 240) {
            error = "package_wallpaper_invalid"; return false;
        }
    }
    std::set<std::string> used;
    for (const auto& unit : out.units) used.insert(unit.resource_id);
    if (!out.wallpaper_resource_id.empty()) used.insert(out.wallpaper_resource_id);
    if (used != ids) { error = "package_resource_unused"; return false; }
    return true;
}
}  // namespace

bool IsAppearancePreset(const std::string& preset) { return preset == "dark" || preset == "light" || preset == "blue" || preset == "green"; }
bool IsAppearanceIdentifier(const std::string& value) {
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
    });
}
const AppearanceResource* AppearancePackageMetadata::FindResource(const std::string& id) const {
    const auto item = std::find_if(resources.begin(), resources.end(), [&](const auto& value) { return value.id == id; });
    return item == resources.end() ? nullptr : &*item;
}
bool DecodeAppearancePackage(const uint8_t* bytes, size_t size, AppearancePackageMetadata& metadata,
                             size_t& payload_offset, std::string& error) {
    metadata = {}; payload_offset = 0; error.clear();
    if (bytes == nullptr || size < 16 || size > kAppearanceMaxPackageBytes || std::memcmp(bytes, "RAP1", 4) != 0 ||
        Le16(bytes + 4) != 1 || Le16(bytes + 6) != 320 || Le16(bytes + 8) != 240) { error = "package_header_invalid"; return false; }
    const size_t json_size = Le32(bytes + 12);
    if (json_size == 0 || json_size > kAppearanceMaxMetadataBytes || json_size > size - 16 ||
        std::memchr(bytes + 16, 0, json_size) != nullptr) { error = "package_metadata_bounds"; return false; }
    if (!DecodeAppearanceMetadata(bytes, reinterpret_cast<const char*>(bytes + 16), json_size, size, metadata, error)) return false;
    payload_offset = 16 + json_size; return true;
}
bool DecodeAppearanceMetadata(const uint8_t* header, const char* json, size_t json_size,
                              size_t package_size, AppearancePackageMetadata& metadata, std::string& error) {
    metadata = {}; error.clear();
    constexpr char kNullEscape[] = "\\u0000";
    if (header == nullptr || json == nullptr || package_size < 16 || package_size > kAppearanceMaxPackageBytes ||
        std::memcmp(header, "RAP1", 4) != 0 || Le16(header + 4) != 1 || Le16(header + 6) != 320 || Le16(header + 8) != 240 ||
        json_size == 0 || json_size > kAppearanceMaxMetadataBytes || json_size != Le32(header + 12) ||
        json_size > package_size - 16 || std::memchr(json, 0, json_size) != nullptr || !Utf8(json, json_size) ||
        std::search(json, json + json_size, kNullEscape, kNullEscape + 6) != json + json_size) { error = "package_header_invalid"; return false; }
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(json, json_size, &end, false);
    while (end != nullptr && end < json + json_size && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
    const bool complete = root != nullptr && end == json + json_size;
    AppearancePackageMetadata decoded;
    const bool ok = complete && Decode(root, Le16(header + 10), package_size - 16 - json_size, decoded, error);
    cJSON_Delete(root);
    if (!ok) { if (error.empty()) error = "package_json_invalid"; return false; }
    metadata = std::move(decoded); return true;
}
}  // namespace rodakos
