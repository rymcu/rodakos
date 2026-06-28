#include "apps/built_in_apps.h"

#include "apps/home/home_app.h"
#include "apps/settings/settings_app.h"
#include "phone_os/phone_app_registry.h"

void RegisterRodakBuiltInApps(PhoneAppRegistry& registry) {
    RegisterHomeApp(registry);
    RegisterSettingsApp(registry);
}
