#include "rodak_appearance_json.h"

// Keep appearance's PSRAM allocator separate from the process-wide cJSON hooks.
#include "../../tests/app_model/third_party/cjson/cJSON.c"

void AppearanceJsonSetAllocator(void* (*allocate)(size_t), void (*release)(void*)) {
    cJSON_Hooks hooks = {allocate, release};
    cJSON_InitHooks(&hooks);
}
