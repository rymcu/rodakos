#include "rodak_appearance.h"
#include "test_framework.h"

#include <cJSON.h>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void AppearanceJsonSetAllocator(void* (*allocate)(size_t), void (*release)(void*));
namespace {
size_t allocations = 0;
size_t releases = 0;
void* CountAllocate(size_t size) { ++allocations; return std::malloc(size); }
void CountRelease(void* value) { if (value != nullptr) ++releases; std::free(value); }
}
RODAK_TEST("appearance JSON allocator is isolated from global cJSON and releases its tree") {
    allocations = releases = 0;
    AppearanceJsonSetAllocator(CountAllocate, CountRelease);
    cJSON* global = cJSON_Parse("{\"global\":true}");
    RODAK_CHECK(global != nullptr); RODAK_CHECK_EQ(allocations, size_t(0));
    const std::string metadata = R"({"schema":"rodak-appearance-v1","width":320,"height":240,"animation":{"kind":"builtin","template":"fade","durationMs":2500,"background":0,"color":0,"units":[]},"theme":{"preset":"dark","primary":0},"resources":[]})";
    std::vector<uint8_t> bytes(16 + metadata.size());
    std::memcpy(bytes.data(), "RAP1", 4); bytes[4] = 1; bytes[6] = 64; bytes[7] = 1; bytes[8] = 240;
    for (size_t i = 0; i < 4; ++i) bytes[12 + i] = static_cast<uint8_t>(metadata.size() >> (i * 8));
    std::memcpy(bytes.data() + 16, metadata.data(), metadata.size());
    rodakos::AppearancePackageMetadata decoded; size_t offset = 0; std::string error;
    const bool ok = rodakos::DecodeAppearancePackage(bytes.data(), bytes.size(), decoded, offset, error);
    cJSON_Delete(global);
    AppearanceJsonSetAllocator(std::malloc, std::free);
    RODAK_CHECK(ok); RODAK_CHECK(allocations > 0); RODAK_CHECK_EQ(allocations, releases);
}
