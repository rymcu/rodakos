#include "test_framework.h"

#include "phone_os/voice_identity.h"
#include "phone_os/voice_wake_settings.h"
#include "settings.h"

#include <limits>

namespace {
using rodakos::VoiceIdentityRecord;
using rodakos::VoiceIdentitySaveStatus;

VoiceIdentityRecord TemporaryRecord() {
    VoiceIdentityRecord record;
    record.active.name = "临时身份";
    record.active.wake_word = "小达克";
    record.active.wake_command = "xiao da ke";
    record.active.mode = rodakos::VoiceIdentityApplyMode::kTemporary;
    record.active.revision = 8;
    record.active.expires_at_ms = 1'800'000'000'000LL;
    record.last_accepted = record.active;
    return record;
}

std::string Encode(const VoiceIdentityRecord& record) {
    std::string value;
    std::string error;
    RODAK_CHECK(rodakos::EncodeVoiceIdentityRecord(record, value, error));
    return value;
}

void Store(const VoiceIdentityRecord& record) {
    rodakos_test::SetCommittedSetting("voice_wake", "identity", Encode(record));
}

void Legacy(const char* prefix, const rodakos::VoiceIdentityConfig& config) {
    const std::string p(prefix);
    const auto set = [&](const char* key, const std::string& value) {
        rodakos_test::SetCommittedSetting("voice_wake", p + key, value);
    };
    set("_name", config.name);
    set("_word", config.wake_word);
    set("_cmd", config.wake_command);
    set("_rev", std::to_string(config.revision));
    set("_mode", config.mode == rodakos::VoiceIdentityApplyMode::kTemporary ? "1" : "0");
    set("_exp", std::to_string(config.expires_at_ms));
}
}  // namespace

RODAK_TEST("Voice identity record preserves active persistent and expiry watermark together") {
    const auto candidate = TemporaryRecord();
    VoiceIdentityRecord decoded;
    std::string error;
    RODAK_CHECK(rodakos::DecodeVoiceIdentityRecord(Encode(candidate), decoded, error));
    RODAK_CHECK(rodakos::VoiceIdentityConfigEquals(candidate.active, decoded.active));
    RODAK_CHECK(rodakos::VoiceIdentityConfigEquals(candidate.persistent, decoded.persistent));
    auto expired = candidate;
    expired.active = expired.persistent;
    RODAK_CHECK(rodakos::DecodeVoiceIdentityRecord(Encode(expired), decoded, error));
    RODAK_CHECK_EQ(decoded.active.revision, 1U);
    RODAK_CHECK_EQ(decoded.last_accepted.revision, 8U);
    RODAK_CHECK(rodakos::VoiceIdentityConfigEquals(decoded.last_accepted, candidate.active));
}

RODAK_TEST("Voice identity record rejects conflicting and mixed revision snapshots") {
    for (int mutation = 0; mutation < 5; ++mutation) {
        auto record = TemporaryRecord();
        if (mutation == 0) record.last_accepted.revision = 1;
        if (mutation == 1) record.active.name = "不同请求";
        if (mutation == 2) record.persistent.mode = rodakos::VoiceIdentityApplyMode::kTemporary;
        if (mutation == 3) record.active.revision = 0;
        if (mutation == 4) record.last_accepted.expires_at_ms = std::numeric_limits<int64_t>::max();
        std::string json, error;
        RODAK_CHECK_FALSE(rodakos::EncodeVoiceIdentityRecord(record, json, error));
        RODAK_CHECK_FALSE(error.empty());
    }
}

RODAK_TEST("Voice identity record rejects corrupt future duplicate and numeric JSON without changing output") {
    const auto original = TemporaryRecord();
    const std::string valid = Encode(original);
    for (const std::string& invalid : {
             std::string("{}"), std::string("[]"), valid + "garbage", valid + std::string(1, '\0'),
             std::string(rodakos::kVoiceIdentityRecordMaxBytes + 1, ' '),
             std::string("{\"schema\":2") + valid.substr(11),
             std::string("{\"schema\":1,\"schema\":1,") + valid.substr(1)}) {
        auto output = original;
        std::string error;
        RODAK_CHECK_FALSE(rodakos::DecodeVoiceIdentityRecord(invalid, output, error));
        RODAK_CHECK(rodakos::VoiceIdentityConfigEquals(output.active, original.active));
    }
    for (const std::string& bad_revision : {"0", "-1", "1.5", "4294967296", "1e999", "\"8\""}) {
        std::string json = valid;
        const auto offset = json.find("\"revision\":8");
        json.replace(offset, std::string("\"revision\":8").size(), "\"revision\":" + bad_revision);
        VoiceIdentityRecord output;
        std::string error;
        RODAK_CHECK_FALSE(rodakos::DecodeVoiceIdentityRecord(json, output, error));
    }
    std::string json = valid;
    json.replace(json.find("临时身份"), std::string("临时身份").size(), "valid\\u0000truncated");
    VoiceIdentityRecord output;
    std::string error;
    RODAK_CHECK_FALSE(rodakos::DecodeVoiceIdentityRecord(json, output, error));
}

RODAK_TEST("Voice identity creates one authoritative key while leaving enabled independent") {
    rodakos_test::ResetSettingsState();
    rodakos_test::SetCommittedBool("voice_wake", "enabled", false);
    VoiceIdentityRecord record;
    std::string error;
    RODAK_CHECK(rodakos::LoadVoiceWakeIdentitySettings(record, error));
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().commit_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().destructor_commit_calls, 0);
    RODAK_CHECK_EQ(rodakos_test::GetCommittedBool("voice_wake", "enabled"), std::optional<bool>{false});
    RODAK_CHECK_FALSE(rodakos_test::GetCommittedSetting("voice_wake", "identity").empty());
    RODAK_CHECK(rodakos_test::GetCommittedSetting("voice_wake", "p_name").empty());
}

RODAK_TEST("Voice identity migrates complete legacy snapshot once without dual writes") {
    rodakos_test::ResetSettingsState();
    const auto legacy = TemporaryRecord();
    Legacy("p", legacy.persistent);
    Legacy("a", legacy.active);
    VoiceIdentityRecord record;
    std::string error;
    RODAK_CHECK(rodakos::LoadVoiceWakeIdentitySettings(record, error));
    RODAK_CHECK_EQ(record.last_accepted.revision, 8U);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    Legacy("a", rodakos::DefaultVoiceIdentityConfig());
    RODAK_CHECK(rodakos::LoadVoiceWakeIdentitySettings(record, error));
    RODAK_CHECK_EQ(record.active.revision, 8U);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
}

RODAK_TEST("Voice identity never treats legacy partial corrupt or wrong type fields as defaults") {
    for (int mutation = 0; mutation < 5; ++mutation) {
        rodakos_test::ResetSettingsState();
        const auto legacy = TemporaryRecord();
        if (mutation != 0) Legacy("p", legacy.persistent);
        Legacy("a", legacy.active);
        if (mutation == 1) rodakos_test::SetCommittedSetting("voice_wake", "p_rev", "-1");
        if (mutation == 2) rodakos_test::SetCommittedSetting("voice_wake", "a_exp", "9999999999999999999999");
        if (mutation == 3) rodakos_test::SetCommittedBool("voice_wake", "a_name", true);
        if (mutation == 4) rodakos_test::SettingsState().string_read_results[5] = SettingsStringReadStatus::kError;
        VoiceIdentityRecord output;
        std::string error;
        RODAK_CHECK_FALSE(rodakos::LoadVoiceWakeIdentitySettings(output, error));
        RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    }
}

RODAK_TEST("Voice identity malformed authoritative record never falls back to valid legacy") {
    for (const auto& invalid : {std::string("{}"), std::string(""), std::string(4097, 'x')}) {
        rodakos_test::ResetSettingsState();
        Legacy("p", rodakos::DefaultVoiceIdentityConfig());
        Legacy("a", rodakos::DefaultVoiceIdentityConfig());
        rodakos_test::SetCommittedSetting("voice_wake", "identity", invalid);
        VoiceIdentityRecord output;
        std::string error;
        RODAK_CHECK_FALSE(rodakos::LoadVoiceWakeIdentitySettings(output, error));
        RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    }
}

RODAK_TEST("Voice identity saves one whole record and verifies it through a fresh reader") {
    rodakos_test::ResetSettingsState();
    Store(VoiceIdentityRecord{});
    const auto candidate = TemporaryRecord();
    std::string error;
    RODAK_CHECK(rodakos::SaveVoiceWakeIdentitySettings(candidate, error) == VoiceIdentitySaveStatus::kSaved);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 1);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().read_calls, 2);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().destructor_commit_calls, 0);
    VoiceIdentityRecord loaded;
    RODAK_CHECK(rodakos::LoadVoiceWakeIdentitySettings(loaded, error));
    RODAK_CHECK(rodakos::VoiceIdentityConfigEquals(loaded.last_accepted, candidate.last_accepted));
}

RODAK_TEST("Voice identity distinguishes a proven unchanged write from uncertain changed storage") {
    for (int failure = 0; failure < 5; ++failure) {
        rodakos_test::ResetSettingsState();
        Store(VoiceIdentityRecord{});
        auto& state = rodakos_test::SettingsState();
        if (failure == 0) state.write_status = SettingsStringWriteStatus::kError;
        if (failure == 1) state.write_status = SettingsStringWriteStatus::kRemoveFailed;
        if (failure == 2) state.commit_result = false;
        if (failure == 3) state.string_read_results[2] = SettingsStringReadStatus::kError;
        if (failure == 4) {
            state.write_status = SettingsStringWriteStatus::kError;
            state.error_write_mutates = true;
        }
        std::string error;
        const auto result = rodakos::SaveVoiceWakeIdentitySettings(TemporaryRecord(), error);
        RODAK_CHECK(result == (failure == 0 ? VoiceIdentitySaveStatus::kUnchanged
                                            : VoiceIdentitySaveStatus::kIndeterminate));
        RODAK_CHECK_EQ(state.destructor_commit_calls, 0);
        RODAK_CHECK_FALSE(error.empty());
    }
}

RODAK_TEST("Voice identity read failure and invalid record prohibit persistence") {
    rodakos_test::ResetSettingsState();
    rodakos_test::SettingsState().read_override = SettingsStringReadStatus::kError;
    std::string error;
    RODAK_CHECK(rodakos::SaveVoiceWakeIdentitySettings(TemporaryRecord(), error) ==
                VoiceIdentitySaveStatus::kIndeterminate);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
    rodakos_test::ResetSettingsState();
    auto invalid = TemporaryRecord();
    invalid.active.name.clear();
    RODAK_CHECK(rodakos::SaveVoiceWakeIdentitySettings(invalid, error) == VoiceIdentitySaveStatus::kUnchanged);
    RODAK_CHECK_EQ(rodakos_test::SettingsState().set_calls, 0);
}
