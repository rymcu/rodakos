#include "rodak_appearance.h"
#include "rodak_appearance_assets.h"
#include "test_framework.h"

#include <cJSON.h>
#include <cstring>
#include <functional>
#include <vector>

namespace {
const char* kMetadata = R"({"schema":"rodak-appearance-v1","width":320,"height":240,"animation":{"kind":"text","template":"letters","durationMs":2500,"background":0,"color":16777215,"units":[{"resourceId":"glyph","x":10,"y":100,"startMs":80,"durationMs":260}]},"theme":{"preset":"dark","primary":7982079},"resources":[{"id":"glyph","format":"a4","width":3,"height":2,"offset":0,"length":4}]})";
std::vector<uint8_t> Package(const std::string& metadata = kMetadata, size_t payload = 4, unsigned count = 1) {
    std::vector<uint8_t> bytes(16 + metadata.size() + payload);
    std::memcpy(bytes.data(), "RAP1", 4); bytes[4] = 1; bytes[6] = 64; bytes[7] = 1; bytes[8] = 240; bytes[10] = static_cast<uint8_t>(count);
    const size_t length = metadata.size();
    for (size_t i = 0; i < 4; ++i) bytes[12 + i] = static_cast<uint8_t>(length >> (i * 8));
    std::memcpy(bytes.data() + 16, metadata.data(), length); return bytes;
}
bool Decode(const std::vector<uint8_t>& bytes) {
    rodakos::AppearancePackageMetadata metadata; size_t offset = 0; std::string error;
    return rodakos::DecodeAppearancePackage(bytes.data(), bytes.size(), metadata, offset, error);
}
std::vector<uint8_t> Mutate(const std::function<void(cJSON*)>& change, size_t payload = 4, unsigned count = 1) {
    cJSON* root = cJSON_Parse(kMetadata); change(root); char* json = cJSON_PrintUnformatted(root);
    auto package = Package(json, payload, count); cJSON_free(json); cJSON_Delete(root); return package;
}
cJSON* Animation(cJSON* root) { return cJSON_GetObjectItemCaseSensitive(root, "animation"); }
cJSON* Resource(cJSON* root) { return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "resources"), 0); }
cJSON* Unit(cJSON* root) { return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(Animation(root), "units"), 0); }
}
RODAK_TEST("appearance codec accepts packed odd-width A4 and exposes metadata") {
    auto bytes = Package(); rodakos::AppearancePackageMetadata metadata; size_t offset = 0; std::string error;
    RODAK_CHECK(rodakos::DecodeAppearancePackage(bytes.data(), bytes.size(), metadata, offset, error));
    RODAK_CHECK_EQ(metadata.resources[0].length, size_t(4)); RODAK_CHECK_EQ(metadata.units.size(), size_t(1));
    RODAK_CHECK_EQ(offset, bytes.size() - 4); RODAK_CHECK_EQ(metadata.theme_preset, std::string("dark"));
}
RODAK_TEST("appearance codec rejects truncated wrong-version and oversized headers") {
    auto bytes = Package(); bytes.resize(15); RODAK_CHECK_FALSE(Decode(bytes));
    bytes = Package(); bytes[4] = 2; RODAK_CHECK_FALSE(Decode(bytes));
    bytes = Package(); bytes[6] = 65; RODAK_CHECK_FALSE(Decode(bytes));
    bytes = Package(); bytes[12] = 255; bytes[13] = 255; RODAK_CHECK_FALSE(Decode(bytes));
    bytes = Package(); bytes[10] = 2; RODAK_CHECK_FALSE(Decode(bytes));
    RODAK_CHECK_FALSE(Decode(Package(kMetadata, 524288)));
}
RODAK_TEST("appearance metadata rejects second JSON and invalid UTF8 while allowing trailing whitespace") {
    RODAK_CHECK_FALSE(Decode(Package(std::string(kMetadata) + "{}")));
    RODAK_CHECK_FALSE(Decode(Package(std::string(kMetadata) + "garbage")));
    RODAK_CHECK(Decode(Package(std::string(kMetadata) + "\n \t")));
    std::string invalid = kMetadata; invalid[2] = static_cast<char>(0xff);
    RODAK_CHECK_FALSE(Decode(Package(invalid)));
}
RODAK_TEST("appearance codec rejects gaps overlaps unknown formats and wrong lengths") {
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_SetNumberValue(cJSON_GetObjectItem(Resource(root), "offset"), 1); })));
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_SetNumberValue(cJSON_GetObjectItem(Resource(root), "length"), 3); })));
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_ReplaceItemInObject(Resource(root), "format", cJSON_CreateString("ttf")); })));
    RODAK_CHECK_FALSE(Decode(Package(kMetadata, 5)));
}
RODAK_TEST("appearance codec rejects offscreen unit unsupported references and invalid timing") {
    for (const char* key : {"x", "y"}) RODAK_CHECK_FALSE(Decode(Mutate([&](cJSON* root) { cJSON_SetNumberValue(cJSON_GetObjectItem(Unit(root), key), -1); })));
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_SetNumberValue(cJSON_GetObjectItem(Unit(root), "x"), 318); })));
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_SetNumberValue(cJSON_GetObjectItem(Unit(root), "durationMs"), 2201); })));
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_SetNumberValue(cJSON_GetObjectItem(Unit(root), "durationMs"), 1.5); })));
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_ReplaceItemInObject(Unit(root), "resourceId", cJSON_CreateString("missing")); })));
}
RODAK_TEST("appearance codec enforces full-size RGB565 wallpaper and resource use") {
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_AddStringToObject(root, "wallpaperResourceId", "glyph"); })));
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_ReplaceItemInObject(Animation(root), "kind", cJSON_CreateString("builtin")); })));
    RODAK_CHECK_FALSE(Decode(Mutate([](cJSON* root) { cJSON_ReplaceItemInObject(Animation(root), "kind", cJSON_CreateString("image")); })));
    const char* metadata = R"({"schema":"rodak-appearance-v1","width":320,"height":240,"animation":{"kind":"builtin","template":"fade","durationMs":2500,"background":0,"color":0,"units":[]},"wallpaperResourceId":"wallpaper","theme":{"preset":"blue","primary":123},"resources":[{"id":"wallpaper","format":"rgb565","width":320,"height":240,"offset":0,"length":153600}]})";
    RODAK_CHECK(Decode(Package(metadata, 153600)));
}
RODAK_TEST("appearance assets retain wallpaper independently from animation buffers") {
    rodakos::AppearanceBootAssets assets;
    assets.metadata.resources = {{"glyph", rodakos::AppearancePixelFormat::kA4, 1, 1, 0, 1}, {"wallpaper", rodakos::AppearancePixelFormat::kRgb565, 320, 240, 1, 153600}};
    assets.metadata.wallpaper_resource_id = "wallpaper";
    assets.buffers = {std::shared_ptr<uint8_t>(new uint8_t[1], std::default_delete<uint8_t[]>()), std::shared_ptr<uint8_t>(new uint8_t[153600], std::default_delete<uint8_t[]>())};
    auto wallpaper = assets.WallpaperBuffer(); const auto* ptr = wallpaper.get(); assets.buffers[0].reset(); assets.buffers[1].reset();
    RODAK_CHECK(ptr != nullptr); RODAK_CHECK_EQ(wallpaper.get(), ptr); RODAK_CHECK_EQ(assets.ResourceData(assets.metadata.resources[0]), nullptr);
}
RODAK_TEST("appearance decoded resource budget accepts its boundary and rejects one extra pixel") {
    const auto at_budget = [](bool extra) {
        const unsigned count = extra ? 7 : 6;
        return Mutate([&](cJSON* root) {
            cJSON* resources = cJSON_CreateArray();
            cJSON* units = cJSON_CreateArray();
            size_t offset = 0;
            for (unsigned i = 0; i < count; ++i) {
                const unsigned width = i < 5 ? 320 : i == 5 ? 64 : 1;
                const unsigned height = i < 5 ? 240 : i == 5 ? 144 : 1;
                const size_t length = ((width + 1) / 2) * height;
                const std::string id = "glyph" + std::to_string(i);
                cJSON* resource = cJSON_CreateObject();
                cJSON_AddStringToObject(resource, "id", id.c_str()); cJSON_AddStringToObject(resource, "format", "a4");
                cJSON_AddNumberToObject(resource, "width", width); cJSON_AddNumberToObject(resource, "height", height);
                cJSON_AddNumberToObject(resource, "offset", offset); cJSON_AddNumberToObject(resource, "length", length);
                cJSON_AddItemToArray(resources, resource); offset += length;
                cJSON* unit = cJSON_CreateObject();
                cJSON_AddStringToObject(unit, "resourceId", id.c_str());
                cJSON_AddNumberToObject(unit, "x", 0); cJSON_AddNumberToObject(unit, "y", 0);
                cJSON_AddNumberToObject(unit, "startMs", 80); cJSON_AddNumberToObject(unit, "durationMs", 260);
                cJSON_AddItemToArray(units, unit);
            }
            cJSON_ReplaceItemInObject(root, "resources", resources);
            cJSON_ReplaceItemInObject(Animation(root), "units", units);
        }, 5 * 38400 + 4608 + (extra ? 1 : 0), count);
    };
    RODAK_CHECK_EQ(rodakos::kAppearanceMaxDecodedBytes, size_t(393216));
    RODAK_CHECK(Decode(at_budget(false)));
    rodakos::AppearancePackageMetadata metadata; size_t offset = 0; std::string error;
    auto oversized = at_budget(true);
    RODAK_CHECK_FALSE(rodakos::DecodeAppearancePackage(oversized.data(), oversized.size(), metadata, offset, error));
    RODAK_CHECK_EQ(error, std::string("package_resource_bounds"));
}
RODAK_TEST("appearance decoded budget retains a full transparent logo plus wallpaper") {
    const char* metadata = R"({"schema":"rodak-appearance-v1","width":320,"height":240,"animation":{"kind":"image","template":"fade","durationMs":2500,"background":0,"color":0,"units":[{"resourceId":"logo","x":0,"y":0,"startMs":0,"durationMs":260}]},"wallpaperResourceId":"wallpaper","theme":{"preset":"dark","primary":123},"resources":[{"id":"logo","format":"rgb565a8","width":320,"height":240,"offset":0,"length":230400},{"id":"wallpaper","format":"rgb565","width":320,"height":240,"offset":230400,"length":153600}]})";
    RODAK_CHECK(Decode(Package(metadata, 384000, 2)));
}
