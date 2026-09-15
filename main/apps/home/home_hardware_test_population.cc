#include "apps/home/home_hardware_test_population.h"

#include "apps/clock/clock_app.h"
#include "phone_os/phone_app_registry.h"
#include "phone_ui/phone_fonts.h"

#include <esp_log.h>

#include <cstdio>
#include <memory>

namespace {
constexpr const char* TAG = "HomeHwPopulation";
constexpr size_t kTargetVisibleApps = 25;
constexpr const char* kBuildMarker = "RODAKOS_HOME_HARDWARE_TEST_POPULATION_ACTIVE";
}

void RegisterHomeHardwareTestPopulation(PhoneAppRegistry& registry) {
    ESP_LOGW(TAG, "%s", kBuildMarker);
    const size_t before = registry.ListHomeApps().size();
    const size_t needed = before < kTargetVisibleApps ? kTargetVisibleApps - before : 0;

    for (size_t index = 0; index < needed; ++index) {
        char id[32] = {};
        char title[24] = {};
        std::snprintf(id, sizeof(id), "home-hwtest-%02u",
                      static_cast<unsigned>(index + 1));
        std::snprintf(title, sizeof(title), "HW Test %02u",
                      static_cast<unsigned>(index + 1));

        registry.Register(PhoneAppDescriptor{
            .id = id,
            .title = title,
            .icon = FONT_AWESOME_CIRCLE_INFO,
            .category = PhoneAppCategory::kTools,
            .capabilities = PhoneCapability::kNetwork,
            .show_on_home = true,
            .aliases = {},
            .create = []() { return std::make_unique<ClockApp>(); },
        });
    }

    ESP_LOGW(TAG, "HOME HARDWARE TEST POPULATION ACTIVE: before=%u added=%u after=%u",
             static_cast<unsigned>(before), static_cast<unsigned>(needed),
             static_cast<unsigned>(registry.ListHomeApps().size()));
}
