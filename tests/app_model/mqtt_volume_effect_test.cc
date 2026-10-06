#include "test_framework.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/mqtt_volume_effect.h"

#include <cJSON.h>
#include <esp_codec_dev.h>

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json Parse(const std::string& text) { return Json(cJSON_Parse(text.c_str()), cJSON_Delete); }
const cJSON* Get(const cJSON* object, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}
cJSON* Field(cJSON* object, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}
std::string Text(const cJSON* value) {
    return cJSON_IsString(value) && value->valuestring != nullptr ? value->valuestring : "";
}
std::string Payload(int volume = 30, uint64_t version = 1,
                    const std::string& effect = "effect", const std::string& dispatch = "dispatch") {
    const auto number = std::to_string(version);
    const auto state = "{\"volume\":" + std::to_string(volume) + "}";
    return "{\"deviceKey\":\"device\",\"shadowVersion\":" + number + ",\"version\":" + number +
        ",\"desired\":" + state + ",\"state\":{\"desired\":" + state +
        "},\"_meta\":{\"rodak/deviceEffect\":{\"schema\":\"rodak.mqtt-volume-effect.v1\","
        "\"effectId\":\"" + effect + "\",\"parametersHash\":\"" + std::string(64, 'a') +
        "\",\"dispatchId\":\"" + dispatch + "\",\"shadowVersion\":" + number +
        ",\"operation\":\"volume.set\",\"requested\":" + state + "}}}";
}
std::string Rewrite(const std::string& payload, const std::function<void(cJSON*)>& rewrite) {
    auto root = Parse(payload);
    RODAK_CHECK(root != nullptr);
    rewrite(root.get());
    char* encoded = cJSON_PrintUnformatted(root.get());
    RODAK_CHECK(encoded != nullptr);
    std::string result(encoded);
    cJSON_free(encoded);
    return result;
}
cJSON* Meta(cJSON* root) { return Field(Field(root, "_meta"), "rodak/deviceEffect"); }
void Replace(cJSON* object, const char* key, cJSON* value) {
    RODAK_CHECK(cJSON_ReplaceItemInObjectCaseSensitive(object, key, value));
}
struct Fixture {
    rodakos::AudioOutputService output;
    rodakos::MqttVolumeEffect effects{&output};
    bool handled = false;
    Fixture() { fake_codec::Reset(); }
    std::string Handle(const std::string& payload) {
        return effects.Handle(payload, "device", handled);
    }
};
void ErrorIs(const std::string& response, const char* expected) {
    auto root = Parse(response);
    RODAK_CHECK_EQ(Text(Get(root.get(), "schema")), "rodakos.mqtt-volume-result.v1");
    RODAK_CHECK_EQ(Text(Get(root.get(), "errorCode")), expected);
    RODAK_CHECK(Get(root.get(), "receipt") == nullptr);
}
}  // namespace

RODAK_TEST("MQTT volume returns correlated deferred software evidence from atomic output") {
    Fixture f;
    auto response = Parse(f.Handle(Payload()));
    RODAK_CHECK(f.handled);
    RODAK_CHECK_EQ(Text(Get(response.get(), "schema")), "rodakos.mqtt-volume-result.v1");
    RODAK_CHECK_EQ(Text(Get(response.get(), "effectId")), "effect");
    RODAK_CHECK_EQ(Text(Get(response.get(), "dispatchId")), "dispatch");
    RODAK_CHECK_EQ(Get(response.get(), "shadowVersion")->valueint, 1);
    RODAK_CHECK(Get(response.get(), "errorCode") == nullptr);
    const auto* receipt = Get(response.get(), "receipt");
    RODAK_CHECK_EQ(Text(Get(receipt, "schema")), "rodakos.volume-receipt.v1");
    RODAK_CHECK_EQ(Text(Get(receipt, "parametersHash")), std::string(64, 'a'));
    RODAK_CHECK_EQ(Text(Get(receipt, "operation")), "volume.set");
    RODAK_CHECK_EQ(Get(Get(receipt, "requested"), "volume")->valueint, 30);
    RODAK_CHECK_EQ(Get(receipt, "previousVolume")->valueint, 60);
    RODAK_CHECK_EQ(Get(receipt, "volume")->valueint, 30);
    RODAK_CHECK_EQ(Get(receipt, "configurationRevision")->valueint, 1);
    RODAK_CHECK_EQ(Text(Get(receipt, "status")), "configured");
    RODAK_CHECK_EQ(Text(Get(receipt, "application")), "deferred");
    RODAK_CHECK_EQ(Text(Get(receipt, "persistence")), "volatile");
    RODAK_CHECK(cJSON_IsFalse(Get(receipt, "physicalVerified")));
    RODAK_CHECK_EQ(f.output.volume(), 30);
    RODAK_CHECK_EQ(fake_codec::opens, 0);
}

RODAK_TEST("MQTT volume codec rejection is cached and a new effect can retry after recovery") {
    Fixture f;
    RODAK_CHECK(f.output.OpenForOwner("test", 16000, 1, 16));
    fake_codec::fail_volume_write = true;
    const auto rejected = f.Handle(Payload());
    auto response = Parse(rejected);
    const auto* receipt = Get(response.get(), "receipt");
    RODAK_CHECK_EQ(Text(Get(receipt, "status")), "rejected");
    RODAK_CHECK_EQ(Text(Get(receipt, "application")), "unverified");
    RODAK_CHECK_EQ(Text(Get(receipt, "errorCode")), "codec-volume-rejected");
    RODAK_CHECK_EQ(Get(receipt, "volume")->valueint, 60);
    RODAK_CHECK_EQ(Get(receipt, "configurationRevision")->valueint, 0);
    fake_codec::fail_volume_write = false;
    const auto writes = fake_codec::volume_writes;
    RODAK_CHECK_EQ(f.Handle(Payload()), rejected);
    RODAK_CHECK_EQ(fake_codec::volume_writes, writes);
    auto accepted = Parse(f.Handle(Payload(30, 2, "retry", "retry-dispatch")));
    RODAK_CHECK_EQ(Text(Get(Get(accepted.get(), "receipt"), "application")), "codec-applied");
    RODAK_CHECK_EQ(f.output.volume(), 30);
}

RODAK_TEST("MQTT volume replay returns original result before watermark without rewriting newer state") {
    Fixture f;
    const auto first = f.Handle(Payload());
    f.Handle(Payload(70, 2, "second"));
    RODAK_CHECK_EQ(f.Handle(Payload()), first);
    RODAK_CHECK_EQ(f.output.volume(), 70);
    ErrorIs(f.Handle(Payload(10, 1, "late")), "stale-shadow");
    ErrorIs(f.Handle(Payload(10, 2, "same-version")), "stale-shadow");
    RODAK_CHECK_EQ(f.output.volume(), 70);
}

RODAK_TEST("MQTT volume rejects conflicts in hash dispatch version and target independently") {
    Fixture f;
    f.Handle(Payload());
    for (const auto& payload : {
        Payload(31), Payload(30, 2), Payload(30, 1, "effect", "another-dispatch"),
        Rewrite(Payload(), [](cJSON* root) {
            Replace(Meta(root), "parametersHash", cJSON_CreateString(std::string(64, 'b').c_str()));
        })
    }) ErrorIs(f.Handle(payload), "effect-conflict");
    RODAK_CHECK_EQ(f.output.volume(), 30);
}

RODAK_TEST("MQTT volume legacy requests retain clamp and do not invent operation receipts") {
    Fixture f;
    RODAK_CHECK(f.Handle(R"({"desired":{"volume":150}})").empty());
    RODAK_CHECK(f.handled);
    RODAK_CHECK_EQ(f.output.volume(), 100);
    RODAK_CHECK(f.Handle(R"({"state":{"desired":{"volume":-10}}})").empty());
    RODAK_CHECK_EQ(f.output.volume(), 0);
    f.Handle(R"({"desired":{"volume":22.8},"_meta":{"unrelated":true}})");
    RODAK_CHECK_EQ(f.output.volume(), 22);
    f.Handle(R"({"desired":{"light":{"enabled":true}}})");
    RODAK_CHECK_FALSE(f.handled);
}

RODAK_TEST("MQTT volume versioned plain desired advances ordering and same version never writes twice") {
    Fixture f;
    RODAK_CHECK(f.output.OpenForOwner("test", 16000, 1, 16));
    f.Handle(R"({"version":10,"desired":{"volume":80}})");
    const auto writes = fake_codec::volume_writes;
    f.Handle(R"({"version":10,"desired":{"volume":20}})");
    f.Handle(R"({"version":9,"desired":{"volume":20}})");
    RODAK_CHECK_EQ(fake_codec::volume_writes, writes);
    ErrorIs(f.Handle(Payload(30, 9)), "stale-shadow");
    RODAK_CHECK_EQ(f.output.volume(), 80);
    auto next = Parse(f.Handle(Payload(30, 11, "next")));
    RODAK_CHECK(Get(next.get(), "receipt") != nullptr);
    RODAK_CHECK_EQ(f.output.volume(), 30);
}

RODAK_TEST("MQTT volume recognizes either desired envelope and version alias") {
    Fixture f;
    auto only_nested = Rewrite(Payload(), [](cJSON* root) {
        cJSON_DeleteItemFromObjectCaseSensitive(root, "desired");
        cJSON_DeleteItemFromObjectCaseSensitive(root, "shadowVersion");
    });
    RODAK_CHECK(Get(Parse(f.Handle(only_nested)).get(), "receipt") != nullptr);
    auto only_primary = Rewrite(Payload(40, 2, "next"), [](cJSON* root) {
        cJSON_DeleteItemFromObjectCaseSensitive(root, "state");
        cJSON_DeleteItemFromObjectCaseSensitive(root, "version");
    });
    RODAK_CHECK(Get(Parse(f.Handle(only_primary)).get(), "receipt") != nullptr);
    RODAK_CHECK_EQ(f.output.volume(), 40);
}

RODAK_TEST("MQTT volume malformed known metadata never falls back to legacy volume") {
    Fixture f;
    for (const auto& meta : {"null", "[]", "false", "\"text\"", "{}"}) {
        f.Handle("{\"desired\":{\"volume\":10},\"_meta\":{\"rodak/deviceEffect\":" + std::string(meta) + "}}");
        RODAK_CHECK(f.handled);
        RODAK_CHECK_EQ(f.output.volume(), 60);
    }
    f.Handle(R"({"desired":{"volume":10},"_meta":null})");
    RODAK_CHECK(f.handled);
    RODAK_CHECK_EQ(f.output.volume(), 60);
}

RODAK_TEST("MQTT volume rejects contradictory envelopes schemas and noninteger correlated arguments") {
    Fixture f;
    const std::vector<std::function<void(cJSON*)>> mutations = {
        [](cJSON* root) { Replace(root, "deviceKey", cJSON_CreateString("other")); },
        [](cJSON* root) { Replace(root, "version", cJSON_CreateNumber(2)); },
        [](cJSON* root) { Replace(root, "shadowVersion", cJSON_CreateNumber(0)); },
        [](cJSON* root) { Replace(Field(root, "desired"), "volume", cJSON_CreateNumber(40)); },
        [](cJSON* root) { Replace(Field(Field(root, "state"), "desired"), "volume", cJSON_CreateNumber(40)); },
        [](cJSON* root) { Replace(root, "state", cJSON_CreateString("invalid")); },
        [](cJSON* root) { Replace(root, "desired", cJSON_CreateArray()); },
        [](cJSON* root) { Replace(Meta(root), "schema", cJSON_CreateString("unknown.v2")); },
        [](cJSON* root) { Replace(Meta(root), "operation", cJSON_CreateString("volume.adjust")); },
        [](cJSON* root) { cJSON_AddStringToObject(Meta(root), "unknown", "field"); },
        [](cJSON* root) { Replace(Field(Meta(root), "requested"), "volume", cJSON_CreateNumber(30.5)); },
        [](cJSON* root) { Replace(Field(Meta(root), "requested"), "volume", cJSON_CreateString("30")); },
        [](cJSON* root) { Replace(Field(Meta(root), "requested"), "volume", cJSON_CreateNumber(101)); },
        [](cJSON* root) { Replace(Field(Meta(root), "requested"), "volume", cJSON_CreateNumber(-1)); },
        [](cJSON* root) { cJSON_AddNumberToObject(Field(Meta(root), "requested"), "step", 10); },
        [](cJSON* root) { cJSON_DeleteItemFromObjectCaseSensitive(Field(Meta(root), "requested"), "volume"); },
        [](cJSON* root) { cJSON_DeleteItemFromObjectCaseSensitive(root, "desired"); cJSON_DeleteItemFromObjectCaseSensitive(root, "state"); }
    };
    for (const auto& mutation : mutations) {
        ErrorIs(f.Handle(Rewrite(Payload(), mutation)), "invalid-request");
        RODAK_CHECK(f.handled);
        RODAK_CHECK_EQ(f.output.volume(), 60);
    }
    RODAK_CHECK(Get(Parse(f.Handle(Payload())).get(), "receipt") != nullptr);
}

RODAK_TEST("MQTT volume rejects ambiguous keys malformed identifiers and unsafe numbers") {
    Fixture f;
    const std::vector<std::function<void(cJSON*)>> mutations = {
        [](cJSON* root) { cJSON_AddStringToObject(root, "deviceKey", "other"); },
        [](cJSON* root) { cJSON_AddNumberToObject(Field(root, "desired"), "volume", 10); },
        [](cJSON* root) { cJSON_AddStringToObject(Meta(root), "effectId", "other"); },
        [](cJSON* root) { cJSON_AddNullToObject(Field(root, "_meta"), "rodak/deviceEffect"); },
        [](cJSON* root) { Replace(Meta(root), "effectId", cJSON_CreateString("bad\"id")); },
        [](cJSON* root) { Replace(Meta(root), "dispatchId", cJSON_CreateString(std::string(129, 'd').c_str())); },
        [](cJSON* root) { Replace(Meta(root), "parametersHash", cJSON_CreateString("not-a-hash")); },
        [](cJSON* root) { Replace(Meta(root), "shadowVersion", cJSON_CreateNumber(9007199254740992.0)); },
        [](cJSON* root) { Replace(Meta(root), "shadowVersion", cJSON_CreateNumber(1.5)); }
    };
    for (const auto& mutation : mutations) {
        const auto response = f.Handle(Rewrite(Payload(), mutation));
        if (!response.empty()) ErrorIs(response, "invalid-request");
        RODAK_CHECK(f.handled);
        RODAK_CHECK_EQ(f.output.volume(), 60);
    }
}

RODAK_TEST("MQTT volume parser bounds depth payload trailing data and decoded NUL") {
    Fixture f;
    for (const auto& payload : {
        std::string(1000, '[') + "0" + std::string(1000, ']'),
        std::string(256 * 1024 + 1, ' '), Payload() + "{}",
        Payload().replace(Payload().find("dispatch\""), 9, "dispatch\\u0000\""),
        std::string("{\"desired\":{\"volume\":10}}") + '\0' + "ignored"
    }) {
        RODAK_CHECK(f.Handle(payload).empty());
        RODAK_CHECK(f.handled);
        RODAK_CHECK_EQ(f.output.volume(), 60);
    }
}

RODAK_TEST("MQTT volume ledger never evicts executed effects at capacity") {
    Fixture f;
    const auto first = f.Handle(Payload());
    for (uint64_t index = 2; index <= 64; ++index)
        f.Handle(Payload(static_cast<int>(index), index, "effect-" + std::to_string(index)));
    RODAK_CHECK_EQ(f.output.volume(), 64);
    ErrorIs(f.Handle(Payload(90, 65, "overflow")), "effect-capacity");
    RODAK_CHECK_EQ(f.Handle(Payload()), first);
    RODAK_CHECK_EQ(f.output.volume(), 64);
}

RODAK_TEST("MQTT volume authority reset releases ledger and watermark without inventing persistence") {
    Fixture f;
    f.Handle(Payload(80, 100));
    f.effects.ResetAuthority();
    auto response = Parse(f.Handle(Payload(20, 1)));
    const auto* receipt = Get(response.get(), "receipt");
    RODAK_CHECK_EQ(Get(receipt, "previousVolume")->valueint, 80);
    RODAK_CHECK_EQ(Get(receipt, "configurationRevision")->valueint, 2);
    RODAK_CHECK_EQ(f.output.volume(), 20);
}

RODAK_TEST("MQTT volume missing output returns cached unknown without a fabricated receipt") {
    rodakos::MqttVolumeEffect effects(nullptr);
    bool handled = false;
    const auto first = effects.Handle(Payload(), "device", handled);
    ErrorIs(first, "outcome-unknown");
    RODAK_CHECK(handled);
    RODAK_CHECK_EQ(effects.Handle(Payload(), "device", handled), first);
}

RODAK_TEST("MQTT volume concurrent duplicate calls share exactly one atomic configuration") {
    Fixture f;
    std::vector<std::string> responses(12);
    std::vector<std::thread> threads;
    for (size_t index = 0; index < responses.size(); ++index) {
        threads.emplace_back([&f, &responses, index]() {
            bool handled = false;
            responses[index] = f.effects.Handle(Payload(), "device", handled);
        });
    }
    for (auto& thread : threads) thread.join();
    for (const auto& response : responses) RODAK_CHECK_EQ(response, responses.front());
    auto root = Parse(responses.front());
    RODAK_CHECK_EQ(Get(Get(root.get(), "receipt"), "configurationRevision")->valueint, 1);
}

RODAK_TEST("MQTT volume accepts safe shadow versions beyond 32 bits without truncating order") {
    Fixture f;
    constexpr uint64_t version = 9007199254740991ULL;
    auto response = Parse(f.Handle(Payload(30, version)));
    RODAK_CHECK_EQ(Get(response.get(), "shadowVersion")->valuedouble, 9007199254740991.0);
    ErrorIs(f.Handle(Payload(40, version - 1, "late")), "stale-shadow");
    RODAK_CHECK_EQ(f.output.volume(), 30);
}

RODAK_TEST("MQTT volume longest valid identities fit complete receipt and replay bounds") {
    Fixture f;
    RODAK_CHECK(f.output.SetVolume(100));
    const auto payload = Payload(0, 1, std::string(128, 'e'), std::string(128, 'd'));
    const auto first = f.Handle(payload);
    auto response = Parse(first);
    RODAK_CHECK_EQ(Text(Get(response.get(), "effectId")), std::string(128, 'e'));
    RODAK_CHECK_EQ(Text(Get(response.get(), "dispatchId")), std::string(128, 'd'));
    RODAK_CHECK_EQ(Text(Get(Get(response.get(), "receipt"), "effectId")), std::string(128, 'e'));
    RODAK_CHECK_EQ(Get(Get(response.get(), "receipt"), "previousVolume")->valueint, 100);
    RODAK_CHECK_EQ(Get(Get(response.get(), "receipt"), "volume")->valueint, 0);
    RODAK_CHECK_EQ(f.Handle(payload), first);
    RODAK_CHECK(f.output.OpenForOwner("test", 16000, 1, 16));
    const auto maximum = f.Handle(Payload(100, 2, std::string(128, 'm'), std::string(128, 'n')));
    auto configured = Parse(maximum);
    RODAK_CHECK_EQ(Get(Get(configured.get(), "receipt"), "volume")->valueint, 100);
    RODAK_CHECK_EQ(Text(Get(Get(configured.get(), "receipt"), "application")), "codec-applied");
    fake_codec::fail_volume_write = true;
    const auto rejected_payload = Payload(100, 3, std::string(128, 'r'), std::string(128, 's'));
    const auto rejected = f.Handle(rejected_payload);
    auto rejection = Parse(rejected);
    const auto* receipt = Get(rejection.get(), "receipt");
    RODAK_CHECK_EQ(Get(receipt, "previousVolume")->valueint, 100);
    RODAK_CHECK_EQ(Get(receipt, "volume")->valueint, 100);
    RODAK_CHECK_EQ(Text(Get(receipt, "errorCode")), "codec-volume-rejected");
    RODAK_CHECK_EQ(f.Handle(rejected_payload), rejected);
}

RODAK_TEST("MQTT volume rejects oversized relevant objects before duplicate scanning") {
    Fixture f;
    const auto payload = Rewrite(Payload(), [](cJSON* root) {
        for (int index = 0; index < 257; ++index)
            cJSON_AddNumberToObject(root, ("field-" + std::to_string(index)).c_str(), index);
    });
    RODAK_CHECK(f.Handle(payload).empty());
    RODAK_CHECK(f.handled);
    RODAK_CHECK_EQ(f.output.volume(), 60);
}
