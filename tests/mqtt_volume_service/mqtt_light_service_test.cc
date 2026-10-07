#include "service_fixture.h"
#include <future>
#include "phone_os/voice_wake_service.h"
#include "phone_os/appearance_service.h"

using namespace mqtt_host;
namespace {
std::string LightRequest(unsigned version = 1, const std::string& patch = R"({"brightness":30})",
                         const std::string& id = "board_rgb") {
    const auto v = std::to_string(version);
    return "{\"deviceKey\":\"test-device\",\"version\":" + v + ",\"shadowVersion\":" + v +
        ",\"desired\":{\"volume\":90,\"light\":{\"id\":\"" + id + "\"," + patch.substr(1) +
        "},\"_meta\":{\"rodak/deviceEffect\":{\"schema\":\"rodak.mqtt-light-effect.v1\","
        "\"effectId\":\"light:" + v + ":12345678-1234-1234-1234-123456789abc\",\"parametersHash\":\"" +
        std::string(64, 'a') + "\",\"dispatchId\":\"dispatch-" + v + "\",\"shadowVersion\":" + v +
        ",\"operation\":\"light.patch\",\"requested\":{\"lightId\":\"" + id + "\",\"patch\":" + patch + "}}}}";
}
rodakos::LightState State(Fixture& fixture, size_t index = 0) {
    rodakos::LightState state;
    RODAK_CHECK(fixture.lights.GetLight(index, state));
    return state;
}
std::string String(const cJSON* value) { return cJSON_IsString(value) ? value->valuestring : ""; }
cJSON* Field(cJSON* root, const char* key) { return cJSON_GetObjectItemCaseSensitive(root, key); }
cJSON* Meta(cJSON* root) { return Field(Field(root, "_meta"), "rodak/deviceEffect"); }
std::string Edit(const std::string& request, const std::function<void(cJSON*)>& edit) {
    auto json = Parse(request);
    edit(json.get());
    char* encoded = cJSON_PrintUnformatted(json.get());
    std::string result(encoded);
    cJSON_free(encoded);
    return result;
}
void Replace(cJSON* root, const char* key, cJSON* value) {
    RODAK_CHECK(cJSON_ReplaceItemInObjectCaseSensitive(root, key, value));
}
void ErrorIs(const std::string& response, const char* error) {
    auto root = Parse(response);
    RODAK_CHECK_EQ(String(Get(root.get(), "schema")), "rodakos.mqtt-light-result.v1");
    RODAK_CHECK_EQ(String(Get(root.get(), "errorCode")), error);
    RODAK_CHECK(Get(root.get(), "receipt") == nullptr);
}
}

RODAK_TEST("MQTT light uses production fragment worker service driver and correlated software receipt") {
    Fixture f;
    f.Start();
    f.Send(LightRequest(1, R"({"enabled":true,"brightness":50,"color":{"r":255,"g":101,"b":0}})"), true);
    auto result = Parse(f.Receipt());
    const auto* receipt = Get(result.get(), "receipt");
    RODAK_CHECK_EQ(String(Get(result.get(), "schema")), "rodakos.mqtt-light-result.v1");
    RODAK_CHECK_EQ(String(Get(receipt, "schema")), "rodakos.light-receipt.v1");
    RODAK_CHECK_EQ(String(Get(receipt, "application")), "driver-applied");
    RODAK_CHECK_EQ(String(Get(Get(receipt, "current"), "id")), "board_rgb");
    RODAK_CHECK_EQ(Get(Get(receipt, "previous"), "brightness")->valueint, 60);
    RODAK_CHECK_EQ(Get(Get(receipt, "current"), "brightness")->valueint, 50);
    RODAK_CHECK(cJSON_IsFalse(Get(receipt, "physicalVerified")));
    RODAK_CHECK_EQ(String(Get(receipt, "persistence")), "volatile");
    RODAK_CHECK_EQ(fake_light::Pixels()[0].red, 128u);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    f.Barrier();
    bool found_report = false;
    for (const auto& publication : Publications()) {
        if (publication.topic.ends_with("/effects/receipt")) RODAK_CHECK(publication.in_sdk_callback);
        if (publication.topic.ends_with("/shadow/report")) {
            auto report = Parse(publication.payload);
            const auto* light = Get(report.get(), "light");
            RODAK_CHECK_EQ(String(Get(light, "id")), "board_rgb");
            RODAK_CHECK(cJSON_IsTrue(Get(light, "available")));
            RODAK_CHECK(Get(report.get(), "effectId") == nullptr);
            found_report = true;
        }
    }
    RODAK_CHECK(found_report);
}

RODAK_TEST("MQTT light applies only requested fields even when desired contains old other values") {
    Fixture f;
    f.Start();
    RODAK_CHECK(f.lights.SetState(0, true, 70, {40, 50, 60}));
    const auto request = Edit(LightRequest(), [](cJSON* root) {
        auto* light = Field(Field(root, "desired"), "light");
        cJSON_AddBoolToObject(light, "enabled", false);
        cJSON_AddItemToObject(light, "color", cJSON_Parse(R"({"r":1,"g":2,"b":3})"));
    });
    f.Send(request);
    f.Receipt();
    RODAK_CHECK(State(f).enabled);
    RODAK_CHECK_EQ(State(f).color.red, 40);
    RODAK_CHECK_EQ(State(f).brightness_percent, 30);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    f.Send(Edit(Request("audio", 40, 2), [](cJSON* root) {
        cJSON_AddItemToObject(Field(root, "desired"), "light", cJSON_Parse(R"({"id":"board_rgb","brightness":99})"));
    }));
    f.Receipt(1);
    RODAK_CHECK_EQ(State(f).brightness_percent, 30);
    RODAK_CHECK_EQ(f.output.volume(), 40);
}

RODAK_TEST("MQTT light legacy partial channels merge inside the real service and emit no effect receipt") {
    Fixture f;
    f.Start();
    RODAK_CHECK(f.lights.SetState(0, true, 60, {100, 110, 120}));
    f.Send(R"({"version":2,"desired":{"light":{"brightness":35,"color":{"g":42}}}})");
    f.Barrier();
    RODAK_CHECK_EQ(State(f).color.red, 100);
    RODAK_CHECK_EQ(State(f).color.green, 42);
    RODAK_CHECK_EQ(State(f).color.blue, 120);
    RODAK_CHECK(State(f).enabled);
    RODAK_CHECK_EQ(State(f).brightness_percent, 35);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Send(LightRequest(1));
    ErrorIs(f.Receipt(), "stale-shadow");
}

RODAK_TEST("MQTT light metadata never falls back to legacy mutation") {
    Fixture f;
    f.Start();
    for (const char* meta : {"null", "{}", "[]", R"({"schema":"other"})"}) {
        f.Send("{\"desired\":{\"volume\":20,\"light\":{\"brightness\":20}},\"_meta\":{\"rodak/deviceEffect\":" +
               std::string(meta) + "}}");
        f.Barrier();
    }
    f.Send(R"({"desired":{"light":{"brightness":20}},"_meta":null})");
    f.Barrier();
    RODAK_CHECK_EQ(State(f).brightness_percent, 60);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 0u);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
}

RODAK_TEST("MQTT light validates requested patch identity desired versions and canonical effect ID") {
    Fixture f;
    f.Start();
    const std::vector<std::function<void(cJSON*)>> edits = {
        [](cJSON* root) { Replace(Meta(root), "effectId", cJSON_CreateString("light:01:12345678-1234-1234-1234-123456789abc")); },
        [](cJSON* root) { Replace(Meta(root), "effectId", cJSON_CreateString("light:1:uuid")); },
        [](cJSON* root) { Replace(Meta(root), "shadowVersion", cJSON_CreateNumber(2)); },
        [](cJSON* root) { Replace(root, "version", cJSON_CreateNumber(2)); },
        [](cJSON* root) { Replace(root, "deviceKey", cJSON_CreateString("other")); },
        [](cJSON* root) { Replace(Field(Meta(root), "requested"), "lightId", cJSON_CreateString("not-real")); },
        [](cJSON* root) { Replace(Field(Meta(root), "requested"), "patch", cJSON_Parse("{}")); },
        [](cJSON* root) { Replace(Field(Meta(root), "requested"), "patch", cJSON_Parse(R"({"brightness":20.5})")); },
        [](cJSON* root) { Replace(Field(Meta(root), "requested"), "patch", cJSON_Parse(R"({"color":{"r":1}})")); },
        [](cJSON* root) { Replace(Field(Field(root, "desired"), "light"), "brightness", cJSON_CreateNumber(31)); },
        [](cJSON* root) { cJSON_AddNumberToObject(Field(Field(Meta(root), "requested"), "patch"), "unexpected", 1); }
    };
    size_t index = 0;
    for (const auto& edit : edits) {
        f.Send(Edit(LightRequest(), edit));
        ErrorIs(f.Receipt(index++), "invalid-request");
    }
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 0u);
    RODAK_CHECK_EQ(State(f).configuration_revision, 0u);
}

RODAK_TEST("MQTT light rejects duplicate keys unsafe JSON and conflicting nested desired") {
    Fixture f;
    f.Start();
    auto duplicate = LightRequest();
    duplicate.insert(1, "\"desired\":{\"light\":{\"brightness\":90}},");
    f.Send(duplicate);
    f.Send(R"({"desired":{"light":{"brightness":20}},"_meta":{"rodak/deviceEffect\u0000":{}}})");
    f.Barrier();
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Send(Edit(LightRequest(), [](cJSON* root) {
        cJSON_AddItemToObject(root, "state", cJSON_Parse(R"({"desired":{"light":{"id":"board_rgb","brightness":42}}})"));
    }));
    ErrorIs(f.Receipt(), "invalid-request");
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 0u);
}

RODAK_TEST("MQTT light caches success and rejects same effect with conflicting patch") {
    Fixture f;
    f.Start();
    f.Send(LightRequest());
    const auto first = f.Receipt();
    f.Send(LightRequest(2, R"({"brightness":80})"));
    f.Receipt(1);
    f.Send(LightRequest());
    RODAK_CHECK_EQ(f.Receipt(2), first);
    f.Send(LightRequest(1, R"({"brightness":70})"));
    ErrorIs(f.Receipt(3), "effect-conflict");
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 2u);
    RODAK_CHECK_EQ(State(f).brightness_percent, 80);
}

RODAK_TEST("MQTT light continuous slider exceeds 64 receipts but evicted IDs cannot reapply or retarget") {
    Fixture f;
    f.Start();
    for (unsigned version = 1; version <= 70; ++version) {
        f.Send(LightRequest(version));
        auto result = Parse(f.Receipt(version - 1));
        RODAK_CHECK(Get(result.get(), "receipt") != nullptr);
    }
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 70u);
    f.Send(LightRequest());
    ErrorIs(f.Receipt(70), "stale-shadow");
    f.Send(LightRequest(1, R"({"brightness":30})", "accent"));
    ErrorIs(f.Receipt(71), "stale-shadow");
    auto changed_version = Edit(LightRequest(71), [](cJSON* root) {
        Replace(Meta(root), "effectId", cJSON_CreateString("light:1:12345678-1234-1234-1234-123456789abc"));
    });
    f.Send(changed_version);
    ErrorIs(f.Receipt(72), "invalid-request");
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 70u);
    RODAK_CHECK_EQ(State(f, 1).configuration_revision, 0u);
    f.Send(LightRequest(71, R"({"brightness":90})", "accent"));
    auto result = Parse(f.Receipt(73));
    RODAK_CHECK(Get(result.get(), "receipt") != nullptr);
    RODAK_CHECK_EQ(State(f, 1).brightness_percent, 90);
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 71u);
}

RODAK_TEST("MQTT light caches driver rejection and reports retained state as unavailable") {
    Fixture f;
    f.Start();
    fake_light::fail_refresh = true;
    f.Send(LightRequest());
    const auto first = f.Receipt();
    auto result = Parse(first);
    const auto* receipt = Get(result.get(), "receipt");
    RODAK_CHECK_EQ(String(Get(receipt, "status")), "rejected");
    RODAK_CHECK_EQ(String(Get(receipt, "application")), "unverified");
    RODAK_CHECK_EQ(String(Get(receipt, "errorCode")), "light-driver-rejected");
    RODAK_CHECK_EQ(Get(Get(receipt, "current"), "brightness")->valueint, 60);
    RODAK_CHECK_EQ(Get(receipt, "configurationRevision")->valueint, 0);
    fake_light::fail_refresh = false;
    f.Send(LightRequest());
    RODAK_CHECK_EQ(f.Receipt(1), first);
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 1u);
    f.Barrier();
    bool unavailable_report = false;
    for (const auto& item : Publications()) {
        if (!item.topic.ends_with("/shadow/report")) continue;
        auto report = Parse(item.payload);
        const auto* light = Get(report.get(), "light");
        if (cJSON_IsFalse(Get(light, "available"))) {
            RODAK_CHECK_NE(Get(light, "last_error")->valueint, ESP_OK);
            RODAK_CHECK_EQ(Get(light, "brightness")->valueint, 60);
            unavailable_report = true;
        }
    }
    RODAK_CHECK(unavailable_report);
    f.Send(LightRequest(2));
    f.Receipt(2);
    RODAK_CHECK(State(f).available);
    RODAK_CHECK_EQ(State(f).configuration_revision, 1u);
}

RODAK_TEST("MQTT light same-client reconnect invalidates queued writes and pending receipts") {
    Fixture f;
    f.Start();
    PauseDequeue(true);
    f.Send(LightRequest());
    RODAK_CHECK(WaitDequeued());
    Disconnect();
    Connect();
    PauseDequeue(false);
    f.Barrier();
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 0u);
    HoldUserEvents(true);
    f.Send(LightRequest());
    RODAK_CHECK(WaitUntil([]() { return PendingUserEvents() != 0; }));
    Disconnect();
    Connect();
    HoldUserEvents(false);
    f.Barrier();
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Send(LightRequest());
    f.Receipt();
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 1u);
}

RODAK_TEST("MQTT light fragments cannot cross connection epochs") {
    Fixture f;
    f.Start();
    const auto request = LightRequest();
    const size_t half = request.size() / 2;
    Fragment(Config().mqtt_topic_shadow_desired, request.substr(0, half), 0, request.size());
    Disconnect();
    Connect();
    Fragment("", request.substr(half), half, request.size());
    f.Barrier();
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 0u);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
}

RODAK_TEST("MQTT light Stop invalidates dequeued writes and drops pending receipt while preserving cache") {
    Fixture f;
    f.Start();
    PauseDequeue(true);
    f.Send(LightRequest());
    RODAK_CHECK(WaitDequeued());
    auto stop = std::async(std::launch::async, [&]() { f.service.Stop(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    PauseDequeue(false);
    stop.get();
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 0u);
    f.Start();
    HoldUserEvents(true);
    f.Send(LightRequest());
    RODAK_CHECK(WaitUntil([]() { return PendingUserEvents() != 0; }));
    f.service.Stop();
    HoldUserEvents(false);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Start();
    f.Send(LightRequest());
    f.Receipt();
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 1u);
}

RODAK_TEST("MQTT light ordinary credential refresh keeps cached outcome but new binding resets it") {
    Fixture f;
    f.Start();
    auto* original = CurrentClient();
    f.Send(LightRequest());
    const auto first = f.Receipt();
    Disconnect();
    auto config = Config();
    config.mqtt_password = "rotated-token";
    SetConfig(config);
    RejectCredentials();
    f.CheckRefreshedClient(original, "rotated-token");
    f.Send(LightRequest());
    RODAK_CHECK_EQ(f.Receipt(1), first);
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 1u);
    f.service.Stop();
    config = Config();
    config.aiot_device_secret = "binding-2";
    SetConfig(config);
    f.Start();
    f.Send(LightRequest());
    auto result = Parse(f.Receipt(2));
    RODAK_CHECK_EQ(Get(Get(result.get(), "receipt"), "configurationRevision")->valueint, 2);
}

RODAK_TEST("MQTT light serializes in-progress driver commit with Stop") {
    Fixture f;
    f.Start();
    std::promise<void> entered, release;
    auto entered_future = entered.get_future();
    auto release_future = release.get_future().share();
    fake_light::before_refresh = [&]() { entered.set_value(); release_future.wait(); };
    f.Send(LightRequest());
    const bool writing = entered_future.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    auto stop = std::async(std::launch::async, [&]() { f.service.Stop(); });
    const bool waiting = stop.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
    release.set_value();
    stop.get();
    fake_light::before_refresh = {};
    RODAK_CHECK(writing);
    RODAK_CHECK(waiting);
    RODAK_CHECK_EQ(State(f).brightness_percent, 30);
    RODAK_CHECK_EQ(State(f).configuration_revision, 1u);
}

RODAK_TEST("MQTT light bounds largest identifiers and both terminal receipt layouts") {
    Fixture f;
    const std::string id(128, 'l');
    fake_light::SetPrimaryId(id);
    f.Start();
    auto make_request = [&](uint64_t version) {
        return Edit(LightRequest(1, R"({"enabled":false,"brightness":100,"color":{"r":255,"g":255,"b":255}})", id),
            [&](cJSON* root) {
                // cJSON's general number printer rounds this boundary to 15 digits;
                // production host frames use exact decimal safe-integer JSON.
                const auto number = std::to_string(version);
                Replace(root, "version", cJSON_CreateRaw(number.c_str()));
                Replace(root, "shadowVersion", cJSON_CreateRaw(number.c_str()));
                Replace(Meta(root), "shadowVersion", cJSON_CreateRaw(number.c_str()));
                const std::string effect = "light:" + std::to_string(version) + ":12345678-1234-1234-1234-123456789abc";
                Replace(Meta(root), "effectId", cJSON_CreateString(effect.c_str()));
                Replace(Meta(root), "dispatchId", cJSON_CreateString(std::string(128, 'd').c_str()));
            });
    };
    f.Send(make_request(9007199254740990ULL));
    auto success = Parse(f.Receipt());
    RODAK_CHECK_EQ(String(Get(Get(success.get(), "receipt"), "status")), "configured");
    RODAK_CHECK_EQ(String(Get(Get(Get(success.get(), "receipt"), "current"), "id")), id);
    fake_light::fail_refresh = true;
    f.Send(make_request(9007199254740991ULL));
    auto failure = Parse(f.Receipt(1));
    RODAK_CHECK_EQ(String(Get(Get(failure.get(), "receipt"), "status")), "rejected");
    RODAK_CHECK_EQ(State(f).configuration_revision, 1u);
}

RODAK_TEST("MQTT light unbind revokes both pending receipts and new writes") {
    Fixture f;
    f.Start();
    HoldUserEvents(true);
    f.Send(LightRequest());
    RODAK_CHECK(WaitUntil([]() { return PendingUserEvents() != 0; }));
    Disconnect();
    auto config = Config();
    config.unbind_pending = true;
    config.has_mqtt_config = false;
    SetConfig(config);
    RejectCredentials();
    Connect();
    HoldUserEvents(false);
    f.Barrier();
    f.Send(LightRequest(2));
    f.Barrier();
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 1u);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
}

RODAK_TEST("MQTT duplicate root or metadata keys cannot enter unrelated legacy services") {
    rodakos::VoiceWakeService voice;
    rodakos::AppearanceService appearance;
    Fixture f;
    f.service.SetVoiceWakeService(&voice);
    f.service.SetAppearanceService(&appearance);
    f.Start();
    const auto request = Edit(LightRequest(), [](cJSON* root) {
        cJSON_AddItemToObject(Field(root, "desired"), "voice_identity", cJSON_Parse(R"({"name":"Example"})"));
        cJSON_AddItemToObject(Field(root, "desired"), "appearance",
            cJSON_Parse(R"({"revision":1,"deploymentId":"deployment","releaseId":"release","keyId":"key","mode":"custom"})"));
    });
    auto duplicate_root = request;
    duplicate_root.insert(1, R"("_meta":{},)");
    f.Send(duplicate_root);
    auto duplicate_meta = request;
    const auto meta_start = duplicate_meta.find(R"("_meta":{)") + std::string(R"("_meta":{)").size();
    duplicate_meta.insert(meta_start, R"("rodak/deviceEffect":null,)");
    f.Send(duplicate_meta);
    f.Barrier();
    RODAK_CHECK_EQ(voice.identity_calls, 0u);
    RODAK_CHECK_EQ(appearance.desired_calls, 0u);
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 0u);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Send(Edit(request, [](cJSON* root) { cJSON_DeleteItemFromObjectCaseSensitive(root, "_meta"); }));
    f.Barrier();
    RODAK_CHECK_EQ(voice.identity_calls, 1u);
    RODAK_CHECK_EQ(appearance.desired_calls, 1u);
}
