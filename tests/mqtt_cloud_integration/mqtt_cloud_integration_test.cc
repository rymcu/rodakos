#include "service_fixture.h"
#include "../server_trust/host_runtime.h"
#include "phone_os/voice_wake_service.h"
#include "phone_os/server_trust_transport.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>

namespace {
using rodakos::DeviceCloudConfig;

void RotateToken(const std::string& token, bool include_voice = false) {
    trust_test::RespondBound();
    const auto url = rodakos::ServerTrustUrlOrigin(trust_test::BootstrapUrl()) +
                     "/api/v1/aiot/devices/auth/token";
    auto* root = cJSON_Parse(trust_test::replies[url].body.c_str());
    auto* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON_ReplaceItemInObjectCaseSensitive(data, "accessToken", cJSON_CreateString(token.c_str()));
    if (include_voice) {
        auto* voice = cJSON_Parse(R"({"schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
            "protocolVersion":1,"transport":"websocket","authMode":"device-token",
            "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
            "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
            "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
            "capabilities":["session","audio-input","audio-output"],
            "events":["session.open","session.ready","input.start","input.stop","wake.detected",
                      "playback.abort","vad","output.start","output.stop","session.end","mcp","error"]})");
        const auto endpoint = "wss://" + trust_test::TestTrust().tls_name + ":9443/voice";
        cJSON_AddStringToObject(voice, "endpoint", endpoint.c_str());
        cJSON_AddItemToObject(data, "realtimeVoice", voice);
    }
    char* json = cJSON_PrintUnformatted(root);
    trust_test::replies[url].body = json;
    cJSON_free(json); cJSON_Delete(root);
}

struct Fixture {
    explicit Fixture(bool pin = true) {
        trust_test::Reset();
        trust_test::SeedBoundLegacy();
        const auto trust = trust_test::TestTrust();
        std::string proof;
        if (pin) {
            RODAK_CHECK_EQ(cloud.SaveSerialProvisioning(trust_test::BootstrapUrl(),
                std::string(64, 'a'), proof, &trust), rodakos::ProvisioningUrlSaveResult::kSaved);
        }
        trust_test::RespondBound();
        mqtt.SetVoiceWakeService(&voice);
    }
    ~Fixture() {
        mqtt_host::SetSdkHook({});
        mqtt.Stop();
        mqtt_host::JoinWorkers();
        trust_test::on_http_open = {};
    }
    void Start() {
        RODAK_CHECK(mqtt.Start());
        RODAK_CHECK(mqtt_host::WaitUntil([&] { return mqtt.IsConnected(); }));
    }
    void SetVoice(bool active) {
        auto state = voice.GetState();
        state.status = active ? rodakos::VoiceWakeStatus::kAssistantActive
                              : rodakos::VoiceWakeStatus::kDisabled;
        voice.SetState(state);
    }
    mqtt_host::ResetGuard reset;
    rodakos::DeviceCloudConfigService cloud;
    rodakos::OtaUpdateService ota;
    rodakos::AudioOutputService output;
    rodakos::VoiceWakeService voice;
    rodakos::UnifiedMqttService mqtt{cloud, ota, &output};
};
}

RODAK_TEST("MQTT reloads a real cloud rotation made while its refreshed credentials are deferred") {
    Fixture f;
    f.Start();
    auto* original = mqtt_host::CurrentClient();
    RotateToken("mqtt-first-rotation");
    trust_test::on_http_open = [&](const auto& url) {
        if (url.find("/auth/token") != std::string::npos) f.SetVoice(true);
    };
    mqtt_host::Disconnect();
    mqtt_host::RejectCredentials();
    RODAK_CHECK(mqtt_host::WaitUntil([&] {
        DeviceCloudConfig snapshot;
        return f.cloud.Load(snapshot) && snapshot.mqtt_password == "mqtt-first-rotation";
    }));
    RotateToken("voice-newer-rotation", true);
    f.cloud.InvalidateAccessTokenFreshness("mqtt-first-rotation");
    DeviceCloudConfig newer;
    RODAK_CHECK(f.cloud.PrepareVoiceConfig(newer));
    f.SetVoice(false);
    RODAK_CHECK(mqtt_host::WaitUntil([&] {
        return mqtt_host::Restarts() != 0 ||
            (mqtt_host::CurrentClient() != original && f.mqtt.IsConnected());
    }));
    RODAK_CHECK_EQ(mqtt_host::Restarts(), 0U);
    RODAK_CHECK_EQ(mqtt_host::ClientSnapshots().back().credential, "voice-newer-rotation");
    RODAK_CHECK_EQ(newer.aiot_device_secret, "existing-device-secret");
}

RODAK_TEST("MQTT admission rejects a real same generation rotation after SDK initialization begins") {
    Fixture f;
    f.Start();
    auto* original = mqtt_host::CurrentClient();
    DeviceCloudConfig before;
    RODAK_CHECK(f.cloud.Load(before));
    RotateToken("superseded-before-attach");
    std::atomic<bool> refreshed{false};
    std::atomic<unsigned> init_calls{0};
    uint32_t newer_generation = 0;
    mqtt_host::SetSdkHook([&](mqtt_host::SdkOperation operation, auto*) {
        if (operation != mqtt_host::SdkOperation::kInit || ++init_calls != 1) return;
        RotateToken("latest-at-attach", true);
        f.cloud.InvalidateAccessTokenFreshness("superseded-before-attach");
        auto voice = std::async(std::launch::async, [&] {
            DeviceCloudConfig newer;
            const bool result = f.cloud.PrepareVoiceConfig(newer);
            newer_generation = newer.cloud_generation;
            return result;
        });
        refreshed = voice.get();
    });
    mqtt_host::Disconnect();
    mqtt_host::RejectCredentials();
    RODAK_CHECK(mqtt_host::WaitUntil([&] {
        const auto clients = mqtt_host::ClientSnapshots();
        return mqtt_host::Restarts() != 0 ||
            (!clients.empty() && clients.back().connected && mqtt_host::CurrentClient() != original);
    }));
    RODAK_CHECK_EQ(mqtt_host::Restarts(), 0U);
    RODAK_CHECK(refreshed);
    RODAK_CHECK_EQ(newer_generation, before.cloud_generation);
    RODAK_CHECK(mqtt_host::CurrentClient() != original);
    const auto clients = mqtt_host::ClientSnapshots();
    RODAK_CHECK_EQ(clients.back().credential, "latest-at-attach");
    const auto events = mqtt_host::LifecycleEvents();
    bool rejected_candidate = false;
    for (const auto& client : clients) {
        if (client.credential != "superseded-before-attach") continue;
        rejected_candidate = true;
        RODAK_CHECK(client.destroyed);
        for (const auto& event : events) {
            RODAK_CHECK(event.client_id != client.id || event.action != "start");
        }
    }
    RODAK_CHECK(rejected_candidate);
}

RODAK_TEST("MQTT cannot attach credentials after real USB provisioning invalidates the loaded generation") {
    Fixture f;
    f.Start();
    RotateToken("cancelled-by-new-generation");
    std::atomic<bool> replaced{false};
    std::atomic<unsigned> init_calls{0};
    mqtt_host::SetSdkHook([&](mqtt_host::SdkOperation operation, auto*) {
        if (operation != mqtt_host::SdkOperation::kInit || ++init_calls != 1) return;
        const auto trust = trust_test::TestTrust();
        std::string proof;
        replaced = f.cloud.SaveSerialProvisioning(trust_test::BootstrapUrl(9555), "", proof,
            &trust) == rodakos::ProvisioningUrlSaveResult::kSaved;
    });
    mqtt_host::Disconnect();
    mqtt_host::RejectCredentials();
    RODAK_CHECK(mqtt_host::WaitUntil([&] {
        const auto clients = mqtt_host::ClientSnapshots();
        return mqtt_host::Restarts() != 0 || (replaced && clients.size() >= 2 &&
            (clients.back().destroyed || clients.back().connected));
    }));
    RODAK_CHECK(replaced);
    const auto events = mqtt_host::LifecycleEvents();
    for (const auto& client : mqtt_host::ClientSnapshots()) {
        if (client.credential != "cancelled-by-new-generation") continue;
        RODAK_CHECK(client.destroyed);
        for (const auto& event : events) {
            RODAK_CHECK(event.client_id != client.id || event.action != "start");
        }
    }
    DeviceCloudConfig current;
    RODAK_CHECK_FALSE(f.cloud.Load(current));
    RODAK_CHECK(current.server_trust_pending);
    RODAK_CHECK_EQ(current.provisioning_url, trust_test::BootstrapUrl(9555));
}

RODAK_TEST("MQTT retains its old client when real credential persistence or rollback fails") {
    for (bool rollback_fails : {false, true}) {
        Fixture f;
        f.Start();
        auto* original = mqtt_host::CurrentClient();
        RotateToken("never-persisted");
        trust_test::write_error_key = "unified_mqtt/password";
        if (!rollback_fails) trust_test::write_error_remaining = 1;
        mqtt_host::Disconnect();
        mqtt_host::RejectCredentials();
        RODAK_CHECK(mqtt_host::WaitUntil([&] {
            return f.cloud.last_error().find("persist") != std::string::npos;
        }));
        RODAK_CHECK_EQ(mqtt_host::Restarts(), 0U);
        RODAK_CHECK_EQ(mqtt_host::CurrentClient(), original);
        RODAK_CHECK_EQ(mqtt_host::ClientSnapshots().size(), 1U);
        DeviceCloudConfig current;
        RODAK_CHECK(f.cloud.Load(current));
        RODAK_CHECK_EQ(current.mqtt_password, "new-token");
        RODAK_CHECK_EQ(current.aiot_device_secret, "existing-device-secret");
    }
}

RODAK_TEST("MQTT starts a real legacy MQTT only cache when AIoT Load is incomplete") {
    Fixture f(false);
    trust_test::strings.erase("device_cloud/access_token");
    trust_test::strings.erase("device_cloud/device_secret");
    trust_test::strings["unified_mqtt/broker_address"] = "192.168.137.1";
    trust_test::strings["unified_mqtt/username"] = "legacy-device";
    trust_test::strings["unified_mqtt/password"] = "legacy-mqtt-token";
    trust_test::strings["unified_mqtt/device_key"] = "legacy-device";
    DeviceCloudConfig legacy;
    RODAK_CHECK_FALSE(f.cloud.Load(legacy));
    RODAK_CHECK(legacy.has_mqtt_config);
    f.Start();
    RODAK_CHECK_EQ(mqtt_host::ClientSnapshots().back().credential, "legacy-mqtt-token");
    RODAK_CHECK_EQ(mqtt_host::Restarts(), 0U);
}

RODAK_TEST("MQTT refuses a real pinned cache that requires missing AIoT credentials") {
    Fixture f;
    DeviceCloudConfig activated;
    RODAK_CHECK(f.cloud.Refresh(activated));
    trust_test::strings.erase("device_cloud/access_token");
    trust_test::replies.clear();
    DeviceCloudConfig incomplete;
    RODAK_CHECK_FALSE(f.cloud.Load(incomplete));
    RODAK_CHECK(incomplete.has_mqtt_config);
    RODAK_CHECK(incomplete.server_requires_bound_identity);
    RODAK_CHECK(f.mqtt.Start());
    RODAK_CHECK(mqtt_host::WaitUntil([&] { return !f.cloud.last_error().empty(); }));
    f.mqtt.Stop();
    mqtt_host::JoinWorkers();
    RODAK_CHECK(mqtt_host::ClientSnapshots().empty());
    RODAK_CHECK_EQ(mqtt_host::Restarts(), 0U);
}
