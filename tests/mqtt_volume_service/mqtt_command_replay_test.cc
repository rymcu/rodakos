#include "service_fixture.h"
#include "phone_os/webrtc_display_service.h"

using namespace mqtt_host;

namespace {
std::string Topic(const std::string& number) { return "devices/test-device/commands/" + number; }
std::string Request(bool display, const std::string& action) {
    return "{\"command\":\"" + std::string(display ? "display" : "camera") + ".stream." +
        action + "\",\"sessionId\":\"replay-session\",\"type\":\"candidate\",\"data\":\"aWNl\"}";
}
std::string Execute(const std::string& number, const std::string& payload) {
    const size_t previous = WirePublications().size();
    Message(Topic(number), payload, true);
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
    esp_mqtt_event_t event; event.event_id = MQTT_USER_EVENT; Deliver(event);
    const auto sent = WirePublications();
    for (size_t index = previous; index < sent.size(); ++index) {
        if (sent[index].topic != Topic(number) + "/ack") continue;
        const auto body = Parse(sent[index].payload);
        const auto* result = Get(body.get(), "result");
        if (Get(result, "cameraStream") == nullptr && Get(result, "displayStream") == nullptr)
            return sent[index].payload;
    }
    RODAK_CHECK(false);
    return {};
}
void Check(const std::string& acknowledgement, const char* status, const char* error = nullptr) {
    const auto body = Parse(acknowledgement);
    RODAK_CHECK_EQ(std::string(Get(body.get(), "status")->valuestring), status);
    if (error != nullptr) RODAK_CHECK_EQ(std::string(Get(body.get(), "errorCode")->valuestring), error);
}
struct Streams {
    rodakos::WebRtcCameraService camera;
    rodakos::WebRtcDisplayService display;
    Fixture mqtt;
    Streams() {
        camera.start_result = display.start_result = true;
        camera.remote_result = display.remote_result = true;
        mqtt.service.SetWebRtcCameraService(&camera);
        mqtt.service.SetWebRtcDisplayService(&display);
        mqtt.Start();
        HoldUserEvents(true);
    }
    rodakos::WebRtcCameraService& Peer(bool is_display) {
        return is_display ? static_cast<rodakos::WebRtcCameraService&>(display) : camera;
    }
};
}

RODAK_TEST("MQTT duplicate stream Start returns its original ACK without another native Start") {
    for (bool display : {false, true}) {
        Streams streams;
        const auto request = Request(display, "start");
        const auto first = Execute("duplicate-start", request);
        Check(first, "ok");
        RODAK_CHECK_EQ(Execute("duplicate-start", request), first);
        RODAK_CHECK_EQ(streams.Peer(display).start_calls.load(), 1u);
        RODAK_CHECK(streams.Peer(display).running);
    }
}

RODAK_TEST("MQTT duplicate stream Stop cannot stop a newer instance with the same session string") {
    for (bool display : {false, true}) {
        Streams streams;
        Check(Execute("before-stop", Request(display, "start")), "ok");
        const auto request = Request(display, "stop");
        const auto first = Execute("duplicate-stop", request);
        Check(first, "ok");
        const auto stops = streams.Peer(display).stop_calls.load();
        Check(Execute("after-stop-new", Request(display, "start")), "ok");
        RODAK_CHECK_EQ(Execute("duplicate-stop", request), first);
        RODAK_CHECK_EQ(streams.Peer(display).stop_calls.load(), stops);
        RODAK_CHECK_EQ(streams.Peer(display).live_instance.load(), 2u);
        RODAK_CHECK(streams.Peer(display).running);
    }
}

RODAK_TEST("MQTT duplicate stream Signal reaches the native peer only once") {
    for (bool display : {false, true}) {
        Streams streams;
        Check(Execute("before-signal", Request(display, "start")), "ok");
        const auto request = Request(display, "signal");
        const auto first = Execute("duplicate-signal", request);
        Check(first, "ok");
        RODAK_CHECK_EQ(Execute("duplicate-signal", request), first);
        RODAK_CHECK_EQ(streams.Peer(display).remote_calls.load(), 1u);
    }
}

RODAK_TEST("MQTT duplicate failed native Start remains failed without retrying hardware") {
    for (bool display : {false, true}) {
        Streams streams;
        auto& peer = streams.Peer(display);
        peer.start_result = false;
        const auto request = Request(display, "start");
        const auto first = Execute("duplicate-failure", request);
        Check(first, "error", display ? "display_stream_start_failed" : "camera_stream_start_failed");
        peer.start_result = true;
        RODAK_CHECK_EQ(Execute("duplicate-failure", request), first);
        RODAK_CHECK_EQ(peer.start_calls.load(), 1u);
        RODAK_CHECK_FALSE(peer.running);
        Check(Execute("explicit-new-attempt", request), "ok");
        RODAK_CHECK_EQ(peer.start_calls.load(), 2u);
    }
}

RODAK_TEST("MQTT command number conflict rejects changed raw bytes before stream side effects") {
    Streams streams;
    const auto original = Request(false, "start");
    const auto first = Execute("conflict", original);
    Check(first, "ok");
    Check(Execute("conflict", " " + original), "error", "command_conflict");
    Check(Execute("conflict", Request(false, "stop")), "error", "command_conflict");
    RODAK_CHECK_EQ(Execute("conflict", original), first);
    RODAK_CHECK_EQ(streams.camera.start_calls.load(), 1u);
    RODAK_CHECK_EQ(streams.camera.stop_calls.load(), 0u);
    RODAK_CHECK(streams.camera.running);
}

RODAK_TEST("MQTT reconnect replays historical Start result without resurrecting the stopped peer") {
    Streams streams;
    const auto request = Request(false, "start");
    const auto first = Execute("reconnect-replay", request);
    Disconnect();
    RODAK_CHECK(WaitUntil([&]() { return !streams.camera.running; }));
    Connect();
    RODAK_CHECK_EQ(Execute("reconnect-replay", request), first);
    RODAK_CHECK_EQ(streams.camera.start_calls.load(), 1u);
    RODAK_CHECK_FALSE(streams.camera.running);
    Check(Execute("reconnect-new-command", request), "ok");
    RODAK_CHECK_EQ(streams.camera.start_calls.load(), 2u);
}

RODAK_TEST("MQTT token rotation preserves cached command outcomes for the same authority") {
    Streams streams;
    const auto request = Request(true, "start");
    const auto first = Execute("token-replay", request);
    Disconnect();
    auto config = Config();
    config.mqtt_password = "rotated-ledger-token";
    SetConfig(config);
    RejectCredentials();
    RODAK_CHECK(WaitUntil([&]() { return CredentialRevision() == 1 && streams.mqtt.service.IsConnected(); }));
    RODAK_CHECK_EQ(Execute("token-replay", request), first);
    RODAK_CHECK_EQ(streams.display.start_calls.load(), 1u);
    RODAK_CHECK_FALSE(streams.display.running);
    Check(Execute("token-new-command", request), "ok");
}

RODAK_TEST("MQTT replacement binding clears command history rather than reusing another authority ACK") {
    Streams streams;
    const auto request = Request(false, "start");
    Check(Execute("binding-replay", request), "ok");
    streams.mqtt.service.Stop();
    auto config = Config();
    config.aiot_device_secret = "new-command-ledger-binding";
    SetConfig(config);
    streams.mqtt.Start();
    Check(Execute("binding-replay", request), "ok");
    RODAK_CHECK_EQ(streams.camera.start_calls.load(), 2u);
    RODAK_CHECK(streams.camera.running);
}
