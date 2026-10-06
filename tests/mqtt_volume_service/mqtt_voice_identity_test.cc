#include "service_fixture.h"
#include "phone_os/voice_wake_service.h"

using namespace mqtt_host;

namespace {
struct IdentityFixture {
    rodakos::VoiceWakeService wake;
    Fixture mqtt;
    IdentityFixture() {
        mqtt.service.SetVoiceWakeService(&wake);
        mqtt.Start();
    }
    void SendIdentity(const std::string& identity) {
        mqtt.Send("{\"desired\":{\"voice_identity\":" + identity + "}}", true);
        mqtt.Barrier();
    }
};

std::vector<std::string> IdentityReports() {
    std::vector<std::string> reports;
    for (const auto& item : Publications()) {
        if (item.topic != Config().mqtt_topic_shadow_report) continue;
        auto body = Parse(item.payload);
        if (Get(body.get(), "voice_identity") != nullptr) reports.push_back(item.payload);
    }
    return reports;
}
std::string LastStatus() {
    const auto reports = IdentityReports();
    if (reports.empty()) return {};
    auto body = Parse(reports.back());
    const auto* status = Get(Get(body.get(), "voice_identity"), "status");
    return cJSON_IsString(status) ? status->valuestring : "";
}
}

RODAK_TEST("MQTT identity parsing preserves full uint32 revision and absolute Unix milliseconds") {
    IdentityFixture f;
    f.SendIdentity(R"({"name":"测试","wakeWord":"你好达克","wakeCommand":"ni hao da ke","mode":"temporary","revision":4294967295,"expiresAtMs":1800000000000})");
    const auto state = f.wake.GetState();
    RODAK_CHECK_EQ(f.wake.identity_calls, 1u);
    RODAK_CHECK_EQ(state.voice_identity.revision, 4294967295u);
    RODAK_CHECK_EQ(state.voice_identity.expires_at_ms, 1800000000000LL);
    auto report = Parse(IdentityReports().back());
    const auto* identity = Get(report.get(), "voice_identity");
    RODAK_CHECK_EQ(Get(identity, "revisionWatermark")->valuedouble, 4294967295.0);
    RODAK_CHECK(cJSON_IsTrue(Get(identity, "activeConfirmed")));
}

RODAK_TEST("MQTT identity parser rejects unsafe numeric values without entering wake application") {
    IdentityFixture f;
    const std::vector<std::string> values = {
        R"({"revision":0})", R"({"revision":-1})", R"({"revision":1.5})",
        R"({"revision":4294967296})", R"({"revision":"2"})",
        R"({"expiresAtMs":-1})", R"({"expiresAtMs":1.5})",
        R"({"expiresAtMs":9007199254740992})", R"({"expiresAtMs":1e300})",
        R"({"expiresAtMs":"1800000000000"})", R"({"mode":"temporary","expiresAtMs":0})"
    };
    for (const auto& identity : values) f.SendIdentity(identity);
    RODAK_CHECK_EQ(f.wake.identity_calls, 0u);
    RODAK_CHECK_EQ(f.wake.rejected_identity_calls, values.size());
    const auto state = f.wake.GetState();
    RODAK_CHECK_EQ(state.voice_identity.name, "罗达克");
    RODAK_CHECK_EQ(state.voice_identity_revision_watermark, 1u);
    RODAK_CHECK(state.voice_identity_active_confirmed);
    RODAK_CHECK_EQ(LastStatus(), "rejected");
}

RODAK_TEST("MQTT identity parser rejects invalid structures control characters and duplicate fields") {
    IdentityFixture f;
    const std::vector<std::string> values = {
        "[]", "null", R"({"name":2})", R"({"mode":"other"})",
        R"({"revision":2,"revision":3})",
        R"({"wakeWord":"a\nb"})", R"({"name":"a\u007fb"})",
        "{\"name\":\"" + std::string(33, 'a') + "\"}"
    };
    for (const auto& identity : values) f.SendIdentity(identity);
    RODAK_CHECK_EQ(f.wake.identity_calls, 0u);
    RODAK_CHECK_EQ(f.wake.rejected_identity_calls, values.size());
    RODAK_CHECK_EQ(LastStatus(), "rejected");
}

RODAK_TEST("MQTT identity NUL escapes are rejected by the existing bounded envelope parser") {
    IdentityFixture f;
    f.SendIdentity(R"({"name":"a\u0000b"})");
    RODAK_CHECK_EQ(f.wake.identity_calls, 0u);
    RODAK_CHECK_EQ(f.wake.rejected_identity_calls, 0u);
    RODAK_CHECK_EQ(f.wake.GetState().voice_identity.name, "罗达克");
}

RODAK_TEST("MQTT identity keeps missing-field defaults and literal backslash text compatible") {
    IdentityFixture f;
    f.SendIdentity(R"({"revision":2,"name":"\\u0000"})");
    const auto state = f.wake.GetState();
    RODAK_CHECK_EQ(f.wake.identity_calls, 1u);
    RODAK_CHECK_EQ(state.voice_identity.name, "\\u0000");
    RODAK_CHECK_EQ(state.voice_identity.wake_word, "你好达克");
    RODAK_CHECK_EQ(state.voice_identity.wake_command, "ni hao da ke");
    RODAK_CHECK_EQ(state.voice_identity.expires_at_ms, 0);
}

RODAK_TEST("MQTT worker publishes expiry and recovery status changes without another desired message") {
    IdentityFixture f;
    auto state = f.wake.GetState();
    state.voice_identity_revision_watermark = 10;
    state.voice_identity_status = "expired";
    f.wake.SetState(state);
    RODAK_CHECK(WaitUntil([] { return LastStatus() == "expired"; }));
    auto expired = Parse(IdentityReports().back());
    const auto* identity = Get(expired.get(), "voice_identity");
    RODAK_CHECK_EQ(Get(identity, "revision")->valueint, 1);
    RODAK_CHECK_EQ(Get(identity, "revisionWatermark")->valueint, 10);
    RODAK_CHECK(cJSON_IsTrue(Get(identity, "activeConfirmed")));
    for (const char* status : {"pending_clock", "recovery_failed"}) {
        state.voice_identity_status = status;
        state.voice_identity_active_confirmed = false;
        state.voice_identity_error = "runtime is not confirmed";
        f.wake.SetState(state);
        RODAK_CHECK(WaitUntil([&] { return LastStatus() == status; }));
        auto report = Parse(IdentityReports().back());
        RODAK_CHECK(cJSON_IsFalse(Get(Get(report.get(), "voice_identity"), "activeConfirmed")));
    }
}

RODAK_TEST("MQTT worker coalesces unchanged identity reports and ignores wake interaction status") {
    IdentityFixture f;
    const auto count = IdentityReports().size();
    auto state = f.wake.GetState();
    state.status = rodakos::VoiceWakeStatus::kAssistantActive;
    f.wake.SetState(state);
    f.mqtt.Barrier();
    f.mqtt.Barrier();
    RODAK_CHECK_EQ(IdentityReports().size(), count);
    state.voice_identity_active_confirmed = false;
    f.wake.SetState(state);
    RODAK_CHECK(WaitUntil([&] { return IdentityReports().size() > count; }));
    f.mqtt.Barrier();
    RODAK_CHECK_EQ(IdentityReports().size(), count + 1);
}

RODAK_TEST("MQTT reconnect publishes the current identity even when its snapshot is unchanged") {
    IdentityFixture f;
    const auto count = IdentityReports().size();
    Disconnect();
    Connect();
    RODAK_CHECK(WaitUntil([&] { return IdentityReports().size() > count; }));
    RODAK_CHECK_EQ(LastStatus(), "applied");
    RODAK_CHECK_EQ(f.wake.identity_calls, 0u);
}
