#include "service_fixture.h"
#include "phone_os/webrtc_camera_service.h"
#include "phone_os/webrtc_display_service.h"

using namespace mqtt_host;

namespace {
std::string Topic(const std::string& number) {
    return "devices/test-device/commands/" + number;
}
void Process(const std::string& number, const std::string& payload = "ping") {
    Message(Topic(number), payload, payload.size() > 1);
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
}
void Drain() {
    esp_mqtt_event_t event;
    event.event_id = MQTT_USER_EVENT;
    Deliver(event);
}
std::vector<Publication> Wire(const std::string& number) {
    std::vector<Publication> result;
    for (const auto& item : WirePublications())
        if (item.topic == Topic(number) + "/ack") result.push_back(item);
    return result;
}
void NoCommandOutbox() {
    for (const auto& item : Publications())
        if (item.topic.find("/commands/") != std::string::npos) {
            RODAK_CHECK_FALSE(item.via_outbox);
            RODAK_CHECK(item.in_sdk_callback);
        }
    for (const auto& item : QueuedPublications())
        RODAK_CHECK(item.topic.find("/commands/") == std::string::npos);
}
struct StreamFixture {
    rodakos::WebRtcCameraService camera;
    rodakos::WebRtcDisplayService display;
    Fixture mqtt;
    const bool is_display;
    explicit StreamFixture(bool display_kind) : is_display(display_kind) {
        Peer().start_result = true;
        if (is_display) mqtt.service.SetWebRtcDisplayService(&display);
        else mqtt.service.SetWebRtcCameraService(&camera);
        mqtt.Start();
        HoldUserEvents(true);
    }
    rodakos::WebRtcCameraService& Peer() {
        return is_display ? static_cast<rodakos::WebRtcCameraService&>(display) : camera;
    }
    std::string Request(const std::string& operation, const std::string& session) {
        return "{\"command\":\"" + std::string(is_display ? "display" : "camera") +
            ".stream." + operation + "\",\"sessionId\":\"" + session + "\"}";
    }
    void Start(const std::string& number, const std::string& session) {
        Process(number, Request("start", session));
    }
};
void Emit(const rodakos::WebRtcCameraService::Callbacks& callbacks) {
    callbacks.signal(ESP_PEER_MSG_TYPE_CANDIDATE, {'i', 'c', 'e'});
    callbacks.state(ESP_PEER_STATE_CONNECTED);
}
}

RODAK_TEST("MQTT host distinguishes queued outbox acceptance from actual reconnect wire delivery") {
    Fixture fixture;
    fixture.Start();
    HoldWire(true);
    auto* original = CurrentClient();
    const std::string topic = "devices/test-device/host-outbox-control";
    RODAK_CHECK(esp_mqtt_client_enqueue(original, topic.c_str(), "old", 3, 0, 0, true) >= 0);
    RODAK_CHECK_EQ(QueuedPublications().size(), size_t{1});
    for (const auto& item : WirePublications()) RODAK_CHECK_NE(item.topic, topic);
    Disconnect();
    HoldWire(false);
    RODAK_CHECK_EQ(QueuedPublications().size(), size_t{1});
    Connect();
    RODAK_CHECK_EQ(CurrentClient(), original);
    RODAK_CHECK(QueuedPublications().empty());
    bool replayed = false;
    for (const auto& item : WirePublications())
        if (item.topic == topic) { replayed = true; RODAK_CHECK(item.via_outbox); }
    RODAK_CHECK(replayed);
}

RODAK_TEST("MQTT command direct QoS0 bypasses held outbox and cannot replay on reconnect") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    HoldWire(true);
    Process("direct");
    RODAK_CHECK(Wire("direct").empty());
    Drain();
    RODAK_CHECK_EQ(Wire("direct").size(), size_t{1});
    NoCommandOutbox();
    Disconnect();
    Connect();
    HoldWire(false);
    Drain();
    RODAK_CHECK_EQ(Wire("direct").size(), size_t{1});
}

RODAK_TEST("MQTT command pending ACK is dropped at same-client reconnect and new epoch publishes") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    auto* original = CurrentClient();
    Process("old-ack");
    RODAK_CHECK(PendingUserEvents() > 0);
    RODAK_CHECK(Wire("old-ack").empty());
    Disconnect();
    Connect();
    Process("new-ack");
    Drain();
    RODAK_CHECK_EQ(CurrentClient(), original);
    RODAK_CHECK(Wire("old-ack").empty());
    RODAK_CHECK_EQ(Wire("new-ack").size(), size_t{1});
    NoCommandOutbox();
}

RODAK_TEST("MQTT command direct failure can synchronously disconnect without deadlock or queued replay") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    Process("write-fails");
    Process("behind-failure");
    FailNextDirectPublishWithDisconnect();
    Drain();
    RODAK_CHECK_FALSE(fixture.service.IsConnected());
    RODAK_CHECK_EQ(DroppedNativeEvents(), 0u);
    RODAK_CHECK_EQ(DirectPublishAttempts().size(), size_t{1});
    RODAK_CHECK(Wire("write-fails").empty());
    RODAK_CHECK(Wire("behind-failure").empty());
    Connect();
    Process("after-failure");
    Drain();
    RODAK_CHECK_EQ(Wire("after-failure").size(), size_t{1});
    RODAK_CHECK(Wire("write-fails").empty());
    RODAK_CHECK(Wire("behind-failure").empty());
    NoCommandOutbox();
}

RODAK_TEST("MQTT old shared one-slot event queue can lose lifecycle events and leak old stream output") {
    StreamFixture fixture(false);
    UseLegacySharedEventQueue();
    fixture.Start("shared-queue-old", "old-session");
    const auto callbacks = fixture.Peer().SavedCallbacks();
    RODAK_CHECK_EQ(PendingUserEvents(), size_t{1});
    RODAK_CHECK_EQ(PendingNativeEvents(), size_t{1});
    Disconnect();
    RODAK_CHECK_EQ(DroppedNativeEvents(), 1u);
    RODAK_CHECK(fixture.mqtt.service.IsConnected());
    Emit(callbacks);
    Connect();
    RODAK_CHECK_EQ(DroppedNativeEvents(), 2u);
    const auto leaked = Wire("shared-queue-old");
    RODAK_CHECK_FALSE(leaked.empty());
    for (const auto& item : leaked) RODAK_CHECK_EQ(item.transport_epoch, 2u);
}

RODAK_TEST("MQTT separate custom queue preserves the one native lifecycle slot during the same race") {
    StreamFixture fixture(false);
    fixture.Start("separate-queue-old", "old-session");
    const auto callbacks = fixture.Peer().SavedCallbacks();
    RODAK_CHECK_EQ(PendingUserEvents(), size_t{1});
    RODAK_CHECK_EQ(PendingNativeEvents(), size_t{0});
    Disconnect();
    RODAK_CHECK_FALSE(fixture.mqtt.service.IsConnected());
    Emit(callbacks);
    Connect();
    RODAK_CHECK(fixture.mqtt.service.IsConnected());
    Emit(callbacks);
    Process("separate-queue-current");
    Drain();
    RODAK_CHECK_EQ(DroppedNativeEvents(), 0u);
    RODAK_CHECK(Wire("separate-queue-old").empty());
    RODAK_CHECK_EQ(Wire("separate-queue-current").size(), size_t{1});
    NoCommandOutbox();
}

RODAK_TEST("MQTT command custom-event failure drops the item instead of leaking into a later event") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    FailNextCustomEvent();
    Process("event-fails");
    Process("event-works");
    Drain();
    RODAK_CHECK(Wire("event-fails").empty());
    RODAK_CHECK_EQ(Wire("event-works").size(), size_t{1});
    RODAK_CHECK_EQ(DirectPublishAttempts().size(), size_t{1});
    NoCommandOutbox();
}

RODAK_TEST("MQTT custom-to-native transfer failure preserves the wakeup for a later SDK iteration") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    Process("transfer-retry");
    FailNextCustomTransfer();
    RODAK_CHECK(RunOneSdkEvent());
    RODAK_CHECK_EQ(PendingUserEvents(), size_t{1});
    RODAK_CHECK_EQ(PendingNativeEvents(), size_t{0});
    RODAK_CHECK(Wire("transfer-retry").empty());
    Process("joined-retry");
    Drain();
    RODAK_CHECK_EQ(Wire("transfer-retry").size(), size_t{1});
    RODAK_CHECK_EQ(Wire("joined-retry").size(), size_t{1});
    RODAK_CHECK_EQ(DroppedNativeEvents(), 0u);
    NoCommandOutbox();
}

RODAK_TEST("MQTT command publication count and topic bounds drop overflow without outbox fallback") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    for (unsigned index = 0; index < 10; ++index) Process("bounded-" + std::to_string(index));
    RODAK_CHECK_EQ(PendingUserEvents(), size_t{1});
    Drain();
    for (unsigned index = 0; index < 10; ++index)
        RODAK_CHECK_EQ(Wire("bounded-" + std::to_string(index)).size(), index < 8 ? size_t{1} : size_t{0});
    const std::string long_number(512, 'x');
    Process(long_number);
    Drain();
    RODAK_CHECK(Wire(long_number).empty());
    Process("after-overflow");
    Drain();
    RODAK_CHECK_EQ(Wire("after-overflow").size(), size_t{1});
    NoCommandOutbox();
}

RODAK_TEST("MQTT command and legacy effect receipts share one custom wake without changing effect outbox semantics") {
    for (bool command_first : {false, true}) {
        Fixture fixture;
        fixture.Start();
        HoldUserEvents(true);
        if (command_first) Process("shared-effect-wake");
        fixture.Send(Request("shared-wake-effect"), true);
        RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
        if (!command_first) Process("shared-effect-wake");
        RODAK_CHECK_EQ(PendingUserEvents(), size_t{1});
        RODAK_CHECK_EQ(ReceiptCount(), size_t{0});
        Drain();
        RODAK_CHECK_EQ(ReceiptCount(), size_t{1});
        RODAK_CHECK_EQ(Wire("shared-effect-wake").size(), size_t{1});
        for (const auto& item : Publications())
            if (item.topic == "devices/test-device/effects/receipt") {
                RODAK_CHECK(item.via_outbox);
                RODAK_CHECK(item.in_sdk_callback);
            }
        NoCommandOutbox();
    }
}

RODAK_TEST("MQTT camera and display synchronous Start callbacks share original ACK topic and direct path") {
    for (bool display : {false, true}) {
        StreamFixture fixture(display);
        fixture.Peer().synchronous_signal = true;
        fixture.Peer().synchronous_state = true;
        fixture.Start("stream-start", "stream-session");
        Drain();
        const auto sent = Wire("stream-start");
        RODAK_CHECK_EQ(sent.size(), size_t{3});
        const auto signal = Parse(sent[0].payload);
        const auto* stream = Get(Get(signal.get(), "result"), display ? "displayStream" : "cameraStream");
        RODAK_CHECK_EQ(std::string(Get(stream, "event")->valuestring), "signal");
        RODAK_CHECK_EQ(std::string(Get(stream, "data")->valuestring), "dj0wDQo=");
        const auto acknowledgement = Parse(sent[2].payload);
        RODAK_CHECK_EQ(std::string(Get(Get(acknowledgement.get(), "result"), "sessionId")->valuestring), "stream-session");
        NoCommandOutbox();
    }
}

RODAK_TEST("MQTT camera and display late callbacks from an old connection cannot publish on the new epoch") {
    for (bool display : {false, true}) {
        StreamFixture fixture(display);
        fixture.Start("old-stream", "old-session");
        const auto old_callbacks = fixture.Peer().SavedCallbacks();
        Drain();
        const auto original = Wire("old-stream").size();
        Emit(old_callbacks);
        Disconnect();
        Connect();
        Emit(old_callbacks);
        Process("stop-old", fixture.Request("stop", "old-session"));
        fixture.Start("new-stream", "new-session");
        const auto new_callbacks = fixture.Peer().SavedCallbacks(1);
        Emit(new_callbacks);
        Drain();
        RODAK_CHECK_EQ(Wire("old-stream").size(), original);
        RODAK_CHECK_EQ(Wire("new-stream").size(), size_t{3});
        NoCommandOutbox();
    }
}

RODAK_TEST("MQTT command ACK keeps its received epoch when Start reconnects before returning") {
    for (bool display : {false, true}) {
        StreamFixture fixture(display);
        fixture.Peer().synchronous_signal = true;
        fixture.Peer().before_start_return = []() { Disconnect(); Connect(); };
        fixture.Start("crossing-start", "old-session");
        Drain();
        RODAK_CHECK(Wire("crossing-start").empty());
        Process("current-ping");
        Drain();
        RODAK_CHECK_EQ(Wire("current-ping").size(), size_t{1});
        NoCommandOutbox();
    }
}

RODAK_TEST("MQTT camera and display old callbacks remain invalid after credential rotation") {
    for (bool display : {false, true}) {
        StreamFixture fixture(display);
        fixture.Start("credential-old", "old-session");
        auto* original = CurrentClient();
        const auto old_callbacks = fixture.Peer().SavedCallbacks();
        Emit(old_callbacks);
        Disconnect();
        auto config = Config();
        config.mqtt_password = "rotated-command-token";
        SetConfig(config);
        RejectCredentials();
        fixture.mqtt.CheckRefreshedClient(original, "rotated-command-token");
        Emit(old_callbacks);
        Process("credential-current");
        Drain();
        RODAK_CHECK(Wire("credential-old").empty());
        RODAK_CHECK_EQ(Wire("credential-current").size(), size_t{1});
        RODAK_CHECK_EQ(Wire("credential-current")[0].client_id, ClientSnapshots().back().id);
        RODAK_CHECK_EQ(Wire("credential-current")[0].credential_revision, 0u);
        NoCommandOutbox();
    }
}

RODAK_TEST("MQTT camera and display callbacks retained across Stop cannot publish into a replacement client") {
    for (bool display : {false, true}) {
        StreamFixture fixture(display);
        fixture.Start("stopped-stream", "stopped-session");
        const auto old_callbacks = fixture.Peer().SavedCallbacks();
        const auto old_client = CurrentClient();
        fixture.mqtt.service.Stop();
        Emit(old_callbacks);
        RODAK_CHECK(DirectPublishAttempts().empty());
        fixture.mqtt.Start();
        RODAK_CHECK_NE(CurrentClient(), old_client);
        Emit(old_callbacks);
        Process("replacement-ping");
        Drain();
        RODAK_CHECK(Wire("stopped-stream").empty());
        RODAK_CHECK_EQ(Wire("replacement-ping").size(), size_t{1});
        NoCommandOutbox();
    }
}

RODAK_TEST("MQTT Stop revokes a command whose Start handler is already running") {
    StreamFixture fixture(false);
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    fixture.Peer().before_start_return = [&]() {
        entered = true;
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    Message(Topic("handler-in-flight"), fixture.Request("start", "held-session"), true);
    const bool did_enter = WaitUntil([&]() { return entered.load(); });
    std::thread stop([&]() { fixture.mqtt.service.Stop(); });
    const bool revoked = WaitUntil([&]() { return !fixture.mqtt.service.IsConnected(); });
    release = true;
    stop.join();
    RODAK_CHECK(did_enter);
    RODAK_CHECK(revoked);
    RODAK_CHECK(Wire("handler-in-flight").empty());
    RODAK_CHECK(DirectPublishAttempts().empty());
    Emit(fixture.Peer().SavedCallbacks());
    RODAK_CHECK(DirectPublishAttempts().empty());
}

RODAK_TEST("MQTT Stop waits for an admitted direct publish on original client and drops subsequent entries") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    Process("admitted");
    Process("not-admitted");
    PauseDirectPublish(true);
    std::thread publisher([]() { Drain(); });
    const bool entered = WaitDirectPublishEntered();
    std::atomic<bool> stopped{false};
    std::thread stop([&]() { fixture.service.Stop(); stopped = true; });
    const bool revoked = WaitUntil([&]() { return !fixture.service.IsConnected(); });
    const bool waited = !stopped;
    PauseDirectPublish(false);
    publisher.join();
    stop.join();
    RODAK_CHECK(entered);
    RODAK_CHECK(revoked);
    RODAK_CHECK(waited);
    RODAK_CHECK_EQ(Wire("admitted").size(), size_t{1});
    RODAK_CHECK(Wire("not-admitted").empty());
    fixture.Start();
    Drain();
    RODAK_CHECK_EQ(Wire("admitted").size(), size_t{1});
    RODAK_CHECK(Wire("not-admitted").empty());
    NoCommandOutbox();
}

RODAK_TEST("MQTT producer during bounded drain schedules a fresh wake for the remaining publication") {
    StreamFixture fixture(false);
    fixture.Start("drain-refill", "refill-session");
    Drain();
    const auto callbacks = fixture.Peer().SavedCallbacks();
    for (unsigned index = 0; index < 8; ++index)
        callbacks.signal(ESP_PEER_MSG_TYPE_CANDIDATE, {'a'});
    RODAK_CHECK_EQ(PendingUserEvents(), size_t{1});
    PauseDirectPublish(true);
    std::thread publisher([]() { RunOneSdkEvent(); });
    const bool entered = WaitDirectPublishEntered();
    std::atomic<bool> produced{false};
    std::thread producer([&]() {
        callbacks.signal(ESP_PEER_MSG_TYPE_CANDIDATE, {'n'});
        produced = true;
    });
    const bool produced_during_publish = WaitUntil([&]() { return produced.load(); });
    PauseDirectPublish(false);
    publisher.join();
    producer.join();
    RODAK_CHECK(entered);
    RODAK_CHECK(produced_during_publish);
    RODAK_CHECK_EQ(Wire("drain-refill").size(), size_t{9});
    RODAK_CHECK_EQ(PendingUserEvents(), size_t{1});
    RODAK_CHECK(RunOneSdkEvent());
    const auto sent = Wire("drain-refill");
    RODAK_CHECK_EQ(sent.size(), size_t{10});
    const auto last = Parse(sent.back().payload);
    const auto* stream = Get(Get(last.get(), "result"), "cameraStream");
    RODAK_CHECK_EQ(std::string(Get(stream, "data")->valuestring), "bg==");
    NoCommandOutbox();
}

RODAK_TEST("MQTT stream publication per-payload and total byte bounds reject excess without fallback") {
    StreamFixture fixture(false);
    fixture.Start("bounded-stream", "bounded-session");
    Drain();
    const auto callbacks = fixture.Peer().SavedCallbacks();
    callbacks.signal(ESP_PEER_MSG_TYPE_SDP, std::vector<uint8_t>(60'000, 'x'));
    Drain();
    RODAK_CHECK_EQ(Wire("bounded-stream").size(), size_t{1});
    for (unsigned index = 0; index < 3; ++index)
        callbacks.signal(ESP_PEER_MSG_TYPE_SDP, std::vector<uint8_t>(35'000, 'y'));
    Drain();
    RODAK_CHECK_EQ(Wire("bounded-stream").size(), size_t{3});
    NoCommandOutbox();
    callbacks.state(ESP_PEER_STATE_CONNECTED);
    Drain();
    RODAK_CHECK_EQ(Wire("bounded-stream").size(), size_t{4});
}
