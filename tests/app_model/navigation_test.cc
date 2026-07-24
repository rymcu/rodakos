#include "test_framework.h"

#include "phone_os/phone_navigation.h"
#include "phone_os/phone_system.h"

#include <string>

RODAK_TEST("navigation forwards the complete public contract to PhoneSystem") {
    PhoneSystem system;
    PhoneNavigation navigation(system);

    system.launch_result = false;
    RODAK_CHECK_FALSE(navigation.Launch("photos"));
    RODAK_CHECK_EQ(system.launch_calls, 1);
    RODAK_CHECK_EQ(system.launched_app_id, std::string("photos"));

    system.refresh_theme_result = false;
    RODAK_CHECK_FALSE(navigation.RefreshTheme());
    RODAK_CHECK_EQ(system.refresh_theme_calls, 1);

    system.return_home_result = true;
    RODAK_CHECK(navigation.ReturnHome());
    RODAK_CHECK_EQ(system.return_home_calls, 1);

    system.lock_result = false;
    RODAK_CHECK_FALSE(navigation.Lock());
    RODAK_CHECK_EQ(system.lock_calls, 1);

    system.toggle_control_center_result = true;
    RODAK_CHECK(navigation.ToggleControlCenter());
    RODAK_CHECK_EQ(system.toggle_control_center_calls, 1);

    system.shell_preferences.lock_on_boot = true;
    system.shell_preferences.control_center_gesture_enabled = false;
    const auto preferences = navigation.GetShellPreferences();
    RODAK_CHECK(preferences.lock_on_boot);
    RODAK_CHECK_FALSE(preferences.control_center_gesture_enabled);
    RODAK_CHECK_EQ(system.get_shell_preferences_calls, 1);

    system.set_lock_on_boot_result = false;
    RODAK_CHECK_FALSE(navigation.SetLockOnBoot(true));
    RODAK_CHECK_EQ(system.set_lock_on_boot_calls, 1);
    RODAK_CHECK(system.lock_on_boot_value);

    system.set_control_center_gesture_result = true;
    RODAK_CHECK(navigation.SetControlCenterGestureEnabled(false));
    RODAK_CHECK_EQ(system.set_control_center_gesture_calls, 1);
    RODAK_CHECK_FALSE(system.control_center_gesture_value);

    system.app_host_state.current_app_id = "music";
    system.app_host_state.current_capabilities = PhoneCapability::kAudioPlayback;
    system.app_host_state.has_current = true;
    const auto state = navigation.GetAppHostState();
    RODAK_CHECK_EQ(state.current_app_id, std::string("music"));
    RODAK_CHECK_EQ(state.current_capabilities, PhoneCapability::kAudioPlayback);
    RODAK_CHECK(state.has_current);
    RODAK_CHECK_EQ(system.get_app_host_state_calls, 1);
}
