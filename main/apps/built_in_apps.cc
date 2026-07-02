#include "apps/built_in_apps.h"

#include "apps/home/home_app.h"
#include "apps/settings/settings_app.h"
#include "apps/photos/photos_app.h"
#include "apps/camera/camera_app.h"
#include "apps/clock/clock_app.h"
#include "apps/file_manager/file_manager_app.h"
#include "apps/music/music_app.h"
#include "apps/assistant/assistant_app.h"
#include "apps/system_info/system_info_app.h"
#include "apps/smart/smart_app.h"
#include "phone_os/phone_app_registry.h"

void RegisterRodakBuiltInApps(PhoneAppRegistry& registry) {
    RegisterHomeApp(registry);
    RegisterSettingsApp(registry);
    RegisterPhotosApp(registry);
    RegisterCameraApp(registry);
    RegisterClockApp(registry);
    RegisterFileManagerApp(registry);
    RegisterSystemInfoApp(registry);
    RegisterMusicApp(registry);
    RegisterAssistantApp(registry);
    RegisterSmartApp(registry);
}
