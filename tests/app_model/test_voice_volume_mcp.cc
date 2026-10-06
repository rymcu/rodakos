#include "test_framework.h"
#include "phone_os/voice_volume_mcp.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/realtime_voice_contract.h"
#include <cJSON.h>
#include <esp_codec_dev.h>
#include <memory>
#include <string>

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json Parse(const std::string& value) { return Json(cJSON_Parse(value.c_str()), cJSON_Delete); }
const cJSON* Get(const cJSON* root, const char* key) { return cJSON_GetObjectItemCaseSensitive(root, key); }
std::string Call(const std::string& id, const char* name = "volume_up",
                 const std::string& args = "{}", const std::string& meta = "") {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id +
        ",\"method\":\"tools/call\",\"params\":{\"name\":\"self.audio_speaker." +
        name + "\",\"arguments\":" + args + (meta.empty() ? "" : ",\"_meta\":" + meta) + "}}";
}
std::string Meta(const char* id = "effect", char hash = 'a') {
    return "{\"rodak/deviceEffect\":{\"effectId\":\"" + std::string(id) +
        "\",\"parametersHash\":\"" + std::string(64, hash) + "\"}}";
}
const std::string kInit = R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{}}})";
struct Fixture {
    rodakos::AudioOutputService output;
    rodakos::VoiceVolumeMcp mcp{output};
    Fixture() { fake_codec::Reset(); RODAK_CHECK(mcp.Bind(1)); RODAK_CHECK(!mcp.Handle(kInit, 1).empty()); }
};
}

RODAK_TEST("Volume MCP requires initialized current scope and preserves RPC id types") {
    rodakos::AudioOutputService output;
    rodakos::VoiceVolumeMcp mcp(output);
    RODAK_CHECK(mcp.Handle(kInit, 1).empty());
    RODAK_CHECK(mcp.Bind(1));
    auto uninitialized = Parse(mcp.Handle(Call("1"), 1));
    RODAK_CHECK(Get(uninitialized.get(), "error") != nullptr);
    mcp.Handle(kInit, 1);
    auto numeric = Parse(mcp.Handle(Call("1"), 1));
    auto string = Parse(mcp.Handle(Call("\"1\""), 1));
    RODAK_CHECK(cJSON_IsNumber(Get(numeric.get(), "id")));
    RODAK_CHECK(cJSON_IsString(Get(string.get(), "id")));
    RODAK_CHECK_EQ(output.volume(), 80);
}

RODAK_TEST("Volume MCP lists exactly three strict tool schemas") {
    Fixture f;
    auto response = Parse(f.mcp.Handle(R"({"jsonrpc":"2.0","id":"list","method":"tools/list","params":{"cursor":"","withUserTools":true}})", 1));
    const cJSON* tools = Get(Get(response.get(), "result"), "tools");
    RODAK_CHECK_EQ(cJSON_GetArraySize(tools), 3);
    const cJSON* schema = Get(cJSON_GetArrayItem(tools, 1), "inputSchema");
    RODAK_CHECK(cJSON_IsFalse(Get(schema, "additionalProperties")));
    RODAK_CHECK_EQ(Get(Get(Get(schema, "properties"), "step"), "default")->valueint, 10);
}

RODAK_TEST("Volume MCP relative calls preserve omitted step and return deferred nonpersistent evidence") {
    Fixture f;
    auto response = Parse(f.mcp.Handle(Call("1", "volume_up", "{}", Meta()), 1));
    const cJSON* receipt = Get(Get(response.get(), "result"), "structuredContent");
    RODAK_CHECK_EQ(f.output.volume(), 70);
    RODAK_CHECK(Get(Get(receipt, "requested"), "step") == nullptr);
    RODAK_CHECK_EQ(Get(receipt, "effectiveStep")->valueint, 10);
    RODAK_CHECK_EQ(std::string(Get(receipt, "application")->valuestring), "deferred");
    RODAK_CHECK_EQ(std::string(Get(receipt, "persistence")->valuestring), "volatile");
    RODAK_CHECK(cJSON_IsFalse(Get(receipt, "physicalVerified")));
    RODAK_CHECK_EQ(fake_codec::opens, 0);
    auto no_meta = Parse(f.mcp.Handle(Call("2", "volume_down", "{\"step\":100}"), 1));
    const cJSON* plain = Get(Get(no_meta.get(), "result"), "structuredContent");
    RODAK_CHECK_EQ(Get(plain, "volume")->valueint, 0);
    RODAK_CHECK(Get(plain, "effectId") == nullptr);
    RODAK_CHECK(Get(plain, "parametersHash") == nullptr);
}

RODAK_TEST("Volume MCP replays RPC and effect identities without repeating relative writes") {
    Fixture f;
    const auto first = f.mcp.Handle(Call("1", "volume_up", "{}", Meta()), 1);
    RODAK_CHECK_EQ(f.mcp.Handle(Call("1", "volume_up", "{}", Meta()), 1), first);
    auto alias = Parse(f.mcp.Handle(Call("2", "volume_up", "{}", Meta()), 1));
    RODAK_CHECK_EQ(Get(alias.get(), "id")->valueint, 2);
    RODAK_CHECK_EQ(f.output.volume(), 70);
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("2", "volume_down", "{}", Meta("other")), 1)).get(), "error") != nullptr);
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("3", "volume_up", "{\"step\":10}", Meta()), 1)).get(), "error") != nullptr);
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("3", "volume_up", "{}", Meta("effect", 'b')), 1)).get(), "error") != nullptr);
    f.mcp.Handle(kInit, 1);
    RODAK_CHECK_EQ(f.mcp.Handle(Call("1", "volume_up", "{}", Meta()), 1), first);
    RODAK_CHECK_EQ(f.output.volume(), 70);
}

RODAK_TEST("Volume MCP never evicts ledger entries and rejects new aliases when full") {
    Fixture f;
    const auto first = f.mcp.Handle(Call("1", "volume_up", "{}", Meta()), 1);
    for (int i = 2; i <= 64; ++i) f.mcp.Handle(Call(std::to_string(i), "volume_down", "{\"step\":1}"), 1);
    const int volume = f.output.volume();
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("65"), 1)).get(), "error") != nullptr);
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("66", "volume_up", "{}", Meta()), 1)).get(), "error") != nullptr);
    RODAK_CHECK_EQ(f.mcp.Handle(Call("1", "volume_up", "{}", Meta()), 1), first);
    RODAK_CHECK_EQ(f.output.volume(), volume);
}

RODAK_TEST("Volume MCP retains rejected codec result and does not retry it after recovery") {
    Fixture f;
    RODAK_CHECK(f.output.OpenForOwner("test", 16000, 1, 16));
    fake_codec::fail_volume_write = true;
    const auto rejected = f.mcp.Handle(Call("1", "set_volume", "{\"volume\":30}", Meta()), 1);
    const auto response = Parse(rejected);
    const cJSON* result = Get(response.get(), "result");
    RODAK_CHECK(cJSON_IsTrue(Get(result, "isError")));
    const cJSON* receipt = Get(result, "structuredContent");
    RODAK_CHECK_EQ(std::string(Get(receipt, "application")->valuestring), "unverified");
    RODAK_CHECK_EQ(Get(receipt, "volume")->valueint, 60);
    fake_codec::fail_volume_write = false;
    RODAK_CHECK_EQ(f.mcp.Handle(Call("1", "set_volume", "{\"volume\":30}", Meta()), 1), rejected);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    auto accepted = Parse(f.mcp.Handle(Call("2", "set_volume", "{\"volume\":30}"), 1));
    RODAK_CHECK_EQ(std::string(Get(Get(Get(accepted.get(), "result"), "structuredContent"), "application")->valuestring), "codec-applied");
}

RODAK_TEST("Volume MCP rejects wrong types unknown args duplicate keys and forged metadata") {
    Fixture f;
    for (const auto& args : {"{\"step\":0}", "{\"step\":101}", "{\"step\":1.5}", "{\"step\":\"10\"}", "{\"delta\":10}", "{\"volume\":10}", "{\"step\":1,\"step\":2}", "[]", "{\"_meta\":{}}"}) {
        RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("1", "volume_up", args), 1)).get(), "error") != nullptr);
    }
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("1", "set_volume", "{}"), 1)).get(), "error") != nullptr);
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("1", "volume_up", "{}", "{\"rodak/deviceEffect\":{\"effectId\":\"forged\"}}"), 1)).get(), "error") != nullptr);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    auto extra = Parse(f.mcp.Handle(Call("1", "volume_up", "{}", "{\"unrelated\":true}"), 1));
    RODAK_CHECK(Get(extra.get(), "result") != nullptr);
}

RODAK_TEST("Volume MCP bounds parse depth and rejects decoded NUL without confusing escaped backslashes") {
    Fixture f;
    const std::string deep = std::string(1000, '[') + "0" + std::string(1000, ']');
    RODAK_CHECK(Get(Parse(f.mcp.Handle(deep, 1)).get(), "error") != nullptr);
    RODAK_CHECK(Get(Parse(f.mcp.Handle(std::string(4097, ' '), 1)).get(), "error") != nullptr);
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("1", "set_volume\\u0000delete", "{\"volume\":30}"), 1)).get(), "error") != nullptr);
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("\"id\\u0000suffix\""), 1)).get(), "error") != nullptr);
    auto escaped = Parse(f.mcp.Handle(Call("\"id\\\\u0000literal\""), 1));
    RODAK_CHECK(Get(escaped.get(), "result") != nullptr);
    RODAK_CHECK_FALSE(rodakos::IsBoundedRealtimeVoiceControlJson(deep));
    RODAK_CHECK(rodakos::IsBoundedRealtimeVoiceControlJson("{\"text\":\"[[]]\\\"{}\"}"));
    RODAK_CHECK_FALSE(rodakos::IsBoundedRealtimeVoiceControlJson("{\"text\":\"\\u0000\"}"));
}

RODAK_TEST("Volume MCP stop and generation replacement cannot be revived by initialize") {
    Fixture f;
    f.mcp.Stop();
    RODAK_CHECK_FALSE(f.mcp.Bind(1));
    RODAK_CHECK(f.mcp.Handle(kInit, 1).empty());
    RODAK_CHECK(f.mcp.Handle(Call("1"), 1).empty());
    RODAK_CHECK(f.mcp.Bind(2));
    RODAK_CHECK(f.mcp.Handle(kInit, 1).empty());
    RODAK_CHECK(Get(Parse(f.mcp.Handle(Call("1"), 2)).get(), "error") != nullptr);
    f.mcp.Handle(kInit, 2);
    int checks = 0;
    RODAK_CHECK(f.mcp.Handle(Call("1"), 2, [&checks]() { return ++checks < 2; }).empty());
    RODAK_CHECK_EQ(f.output.volume(), 60);
    f.mcp.Handle(Call("2"), 2);
    RODAK_CHECK_EQ(f.output.volume(), 70);
}
