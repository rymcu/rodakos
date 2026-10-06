#include "service_fixture.h"

namespace {
std::string Command(mqtt_host::Fixture& fixture, const std::string& payload,
                    const std::string& command_no = "") {
    static unsigned sequence = 0;
    const std::string topic = "devices/test-device/commands/" +
        (command_no.empty() ? "command-test-" + std::to_string(++sequence) : command_no);
    const size_t previous = mqtt_host::Publications().size();
    mqtt_host::Message(topic, payload, payload.size() > 1);
    fixture.Barrier();
    RODAK_CHECK(mqtt_host::WaitUntil([&]() {
        const auto items = mqtt_host::Publications();
        for (size_t index = previous; index < items.size(); ++index)
            if (items[index].topic == topic + "/ack") return true;
        return false;
    }));
    const auto publications = mqtt_host::Publications();
    std::string result;
    for (size_t index = previous; index < publications.size(); ++index) {
        if (publications[index].topic != topic + "/ack") continue;
        RODAK_CHECK(result.empty());
        result = publications[index].payload;
    }
    RODAK_CHECK_FALSE(result.empty());
    return result;
}

void CheckFailure(const std::string& payload, const std::string& code) {
    const auto ack = mqtt_host::Parse(payload);
    RODAK_CHECK_EQ(std::string(cJSON_GetStringValue(mqtt_host::Get(ack.get(), "status"))), "error");
    RODAK_CHECK_EQ(std::string(cJSON_GetStringValue(mqtt_host::Get(ack.get(), "errorCode"))), code);
    RODAK_CHECK(mqtt_host::Get(ack.get(), "result") == nullptr);
}
}

RODAK_TEST("MQTT command handler accepts production ping shapes through fragmented input") {
    mqtt_host::Fixture fixture;
    fixture.Start();
    for (const auto* input : {"ping", "\"ping\"", "{\"command\":\"ping\"}", "{\"type\":\"ping\"}"}) {
        const auto ack = mqtt_host::Parse(Command(fixture, input));
        RODAK_CHECK_EQ(std::string(cJSON_GetStringValue(mqtt_host::Get(ack.get(), "status"))), "ok");
        const auto* result = mqtt_host::Get(ack.get(), "result");
        RODAK_CHECK(cJSON_IsTrue(mqtt_host::Get(result, "pong")));
        RODAK_CHECK(cJSON_IsString(mqtt_host::Get(result, "firmware")));
    }
}

RODAK_TEST("MQTT command handler rejects unsupported and malformed requests") {
    mqtt_host::Fixture fixture;
    fixture.Start();
    for (const auto* input : {"reboot", "{", "[]", "{\"action\":\"ping\"}",
                             "{\"command\":\"unknown\",\"type\":\"ping\"}"}) {
        CheckFailure(Command(fixture, input), "unsupported_command");
    }
}

RODAK_TEST("MQTT command handler reports unavailable camera and display services") {
    mqtt_host::Fixture fixture;
    fixture.Start();
    for (const std::string kind : {"camera", "display"}) {
        for (const std::string operation : {"start", "stop", "signal"}) {
            CheckFailure(Command(fixture, "{\"command\":\"" + kind + ".stream." + operation +
                "\",\"sessionId\":\"host-session\"}"), kind + "_stream_unavailable");
        }
    }
}

RODAK_TEST("MQTT command replay returns its first ACK and rejects conflicting raw payload") {
    mqtt_host::Fixture fixture;
    fixture.Start();
    const auto first = Command(fixture, "ping", "replay-command");
    RODAK_CHECK_EQ(Command(fixture, "ping", "replay-command"), first);
    CheckFailure(Command(fixture, "{\"command\":\"unknown\"}", "replay-command"), "command_conflict");
}

RODAK_TEST("MQTT command queued before reconnect is rejected before handler entry") {
    mqtt_host::Fixture fixture;
    fixture.Start();
    mqtt_host::PauseDequeue(true);
    mqtt_host::Message("devices/test-device/commands/old-epoch", "ping", true);
    RODAK_CHECK(mqtt_host::WaitDequeued());
    mqtt_host::Disconnect();
    mqtt_host::Connect();
    mqtt_host::PauseDequeue(false);
    fixture.Barrier();
    for (const auto& publication : mqtt_host::Publications())
        RODAK_CHECK_NE(publication.topic, "devices/test-device/commands/old-epoch/ack");
    const auto ack = mqtt_host::Parse(Command(fixture, "ping", "current-epoch"));
    RODAK_CHECK_EQ(std::string(cJSON_GetStringValue(mqtt_host::Get(ack.get(), "status"))), "ok");
}
