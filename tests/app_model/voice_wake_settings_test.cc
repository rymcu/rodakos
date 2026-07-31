#include "test_framework.h"

#include "phone_os/voice_wake_settings.h"
#include "settings.h"

#include <optional>

namespace {
constexpr const char* kNamespace = "voice_wake";
constexpr const char* kEnabledKey = "enabled";
}

RODAK_TEST("Voice wake settings initialize a missing key as enabled") {
    rodakos_test::ResetSettingsState();

    bool enabled = false;
    RODAK_CHECK(rodakos::LoadVoiceWakeSettings(enabled));
    RODAK_CHECK(enabled);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().read_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().destructor_commit_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::GetCommittedBool(kNamespace, kEnabledKey),
                   std::optional<bool>{true});
}

RODAK_TEST("Voice wake settings load persisted enabled without writing") {
    rodakos_test::ResetSettingsState();
    rodakos_test::SetCommittedBool(kNamespace, kEnabledKey, true);

    bool enabled = false;
    RODAK_CHECK(rodakos::LoadVoiceWakeSettings(enabled));
    RODAK_CHECK(enabled);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
}

RODAK_TEST("Voice wake settings load persisted disabled without writing") {
    rodakos_test::ResetSettingsState();
    rodakos_test::SetCommittedBool(kNamespace, kEnabledKey, false);

    bool enabled = true;
    RODAK_CHECK(rodakos::LoadVoiceWakeSettings(enabled));
    RODAK_CHECK_FALSE(enabled);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
}

RODAK_TEST("Voice wake settings reject read errors without writing") {
    rodakos_test::ResetSettingsState();
    rodakos_test::SettingsState().bool_read_override = SettingsBoolReadStatus::kError;

    bool enabled = false;
    RODAK_CHECK_FALSE(rodakos::LoadVoiceWakeSettings(enabled));
    RODAK_CHECK_FALSE(enabled);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
}

RODAK_TEST("Voice wake settings reject type mismatches without writing") {
    rodakos_test::ResetSettingsState();
    rodakos_test::SetCommittedSetting(kNamespace, kEnabledKey, "true");

    bool enabled = false;
    RODAK_CHECK_FALSE(rodakos::LoadVoiceWakeSettings(enabled));
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
}

RODAK_TEST("Voice wake settings report missing-key set failures") {
    rodakos_test::ResetSettingsState();
    rodakos_test::SettingsState().bool_set_result = false;

    bool enabled = false;
    RODAK_CHECK_FALSE(rodakos::LoadVoiceWakeSettings(enabled));
    RODAK_CHECK_FALSE(enabled);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 0);
    RODAK_CHECK_FALSE(rodakos_test::GetCommittedBool(kNamespace, kEnabledKey).has_value());
}

RODAK_TEST("Voice wake settings report missing-key commit failures") {
    rodakos_test::ResetSettingsState();
    rodakos_test::SettingsState().commit_result = false;

    bool enabled = false;
    RODAK_CHECK_FALSE(rodakos::LoadVoiceWakeSettings(enabled));
    RODAK_CHECK_FALSE(enabled);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().destructor_commit_calls, 0);
}

RODAK_TEST("Voice wake settings save enabled with an explicit commit") {
    rodakos_test::ResetSettingsState();

    RODAK_CHECK(rodakos::SaveVoiceWakeSettings(true));
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().destructor_commit_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::GetCommittedBool(kNamespace, kEnabledKey),
                   std::optional<bool>{true});
}

RODAK_TEST("Voice wake settings save disabled with an explicit commit") {
    rodakos_test::ResetSettingsState();

    RODAK_CHECK(rodakos::SaveVoiceWakeSettings(false));
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().destructor_commit_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::GetCommittedBool(kNamespace, kEnabledKey),
                   std::optional<bool>{false});
}
