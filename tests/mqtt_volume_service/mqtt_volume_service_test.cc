#include "service_fixture.h"

using namespace mqtt_host;

RODAK_TEST("MQTT service assembles fragments and emits a separate correlated receipt on the SDK task") {
    Fixture f;
    f.Start();
    f.Send(Request(), true);
    auto result = Parse(f.Receipt());
    const cJSON* receipt = Get(result.get(), "receipt");
    RODAK_CHECK(receipt != nullptr);
    RODAK_CHECK_EQ(std::string(Get(receipt, "application")->valuestring), "deferred");
    RODAK_CHECK_EQ(Get(receipt, "volume")->valueint, 30);
    RODAK_CHECK_EQ(f.output.volume(), 30);
    RODAK_CHECK_EQ(fake_codec::volume_writes, 0);
    f.Barrier();
    for (const auto& item : Publications()) {
        if (item.topic == "devices/test-device/effects/receipt") RODAK_CHECK(item.in_sdk_callback);
        if (item.topic == "devices/test-device/shadow/report") {
            auto report = Parse(item.payload);
            RODAK_CHECK(Get(report.get(), "receipt") == nullptr);
            RODAK_CHECK(Get(report.get(), "effectId") == nullptr);
        }
    }
}

RODAK_TEST("MQTT service replays the cached outcome before considering the newer shadow watermark") {
    Fixture f;
    f.Start();
    f.Send(Request());
    const std::string first = f.Receipt();
    f.Send(R"({"version":5,"desired":{"volume":80}})");
    f.Barrier();
    f.Send(Request());
    RODAK_CHECK_EQ(f.Receipt(1), first);
    RODAK_CHECK_EQ(f.output.volume(), 80);
    f.Send(Request("effect-old", 20, 4));
    auto stale = Parse(f.Receipt(2));
    RODAK_CHECK_EQ(std::string(Get(stale.get(), "errorCode")->valuestring), "stale-shadow");
    RODAK_CHECK_EQ(f.output.volume(), 80);
}

RODAK_TEST("MQTT service rejects malformed effect metadata without falling back to legacy volume") {
    Fixture f;
    f.Start();
    f.Send(R"({"desired":{"volume":11},"_meta":{"rodak/deviceEffect":{}}})");
    f.Barrier();
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Send(R"({"state":{"desired":{"volume":123.5}}})");
    f.Barrier();
    RODAK_CHECK_EQ(f.output.volume(), 100);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
}

RODAK_TEST("MQTT service Stop cancels a message already removed from the worker queue") {
    Fixture f;
    f.Start();
    PauseDequeue(true);
    f.Send(Request());
    RODAK_CHECK(WaitDequeued());
    std::thread stop([&]() { f.service.Stop(); });
    const bool cancelled = WaitUntil([&]() { return !f.service.IsConnected(); });
    PauseDequeue(false);
    stop.join();
    RODAK_CHECK(cancelled);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
}

RODAK_TEST("MQTT service rejects an old queued message after the same SDK client reconnects") {
    Fixture f;
    f.Start();
    auto* client = CurrentClient();
    PauseDequeue(true);
    f.Send(Request());
    RODAK_CHECK(WaitDequeued());
    Disconnect();
    Connect();
    PauseDequeue(false);
    f.Barrier();
    RODAK_CHECK_EQ(CurrentClient(), client);
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Send(Request());
    f.Receipt();
    RODAK_CHECK_EQ(f.output.volume(), 30);
}

RODAK_TEST("MQTT service never assembles one desired payload across a disconnect boundary") {
    Fixture f;
    f.Start();
    const std::string payload = Request();
    const int middle = static_cast<int>(payload.size() / 2);
    Fragment(Config().mqtt_topic_shadow_desired, payload.substr(0, middle), 0, payload.size());
    Disconnect();
    Connect();
    Fragment("", payload.substr(middle), middle, payload.size());
    f.Barrier();
    RODAK_CHECK_EQ(f.output.volume(), 60);
    f.Send(payload, true);
    f.Receipt();
    RODAK_CHECK_EQ(f.output.volume(), 30);
}

RODAK_TEST("MQTT service drops a pending old-epoch receipt but retains the executed effect ledger") {
    Fixture f;
    f.Start();
    HoldUserEvents(true);
    f.Send(Request());
    RODAK_CHECK(WaitUntil([]() { return PendingUserEvents() != 0; }));
    RODAK_CHECK_EQ(f.output.volume(), 30);
    Disconnect();
    Connect();
    HoldUserEvents(false);
    f.Barrier();
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Send(Request());
    auto replay = Parse(f.Receipt());
    RODAK_CHECK_EQ(Get(Get(replay.get(), "receipt"), "configurationRevision")->valueint, 1);
    RODAK_CHECK_EQ(f.output.volume(), 30);
}

RODAK_TEST("MQTT service credential refresh rejects late output and preserves same-authority deduplication") {
    Fixture f;
    f.Start();
    HoldUserEvents(true);
    f.Send(Request());
    RODAK_CHECK(WaitUntil([]() { return PendingUserEvents() != 0; }));
    Disconnect();
    auto config = Config();
    config.mqtt_password = "test-token-2";
    SetConfig(config);
    RejectCredentials();
    RODAK_CHECK(WaitUntil([&]() { return CredentialRevision() == 1 && f.service.IsConnected(); }));
    HoldUserEvents(false);
    f.Barrier();
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    RODAK_CHECK_EQ(Restarts(), 0u);
    f.Send(Request());
    auto replay = Parse(f.Receipt());
    RODAK_CHECK_EQ(Get(Get(replay.get(), "receipt"), "configurationRevision")->valueint, 1);
    RODAK_CHECK_EQ(f.output.volume(), 30);
}

RODAK_TEST("MQTT service isolates the effect ledger when a new binding is applied") {
    Fixture f;
    f.Start();
    f.Send(Request());
    f.Receipt();
    f.service.Stop();
    auto config = Config();
    config.aiot_device_secret = "test-binding-2";
    SetConfig(config);
    f.Start();
    f.Send(Request());
    auto result = Parse(f.Receipt(1));
    RODAK_CHECK_EQ(Get(Get(result.get(), "receipt"), "configurationRevision")->valueint, 2);
}

RODAK_TEST("MQTT service reports codec failure once and does not rewrite on a duplicate") {
    Fixture f;
    RODAK_CHECK(f.output.OpenForOwner("test", 16000, 1, 16));
    fake_codec::fail_volume_write = true;
    f.Start();
    f.Send(Request());
    const std::string rejected = f.Receipt();
    auto receipt = Parse(rejected);
    RODAK_CHECK_EQ(std::string(Get(Get(receipt.get(), "receipt"), "status")->valuestring), "rejected");
    const int writes = fake_codec::volume_writes;
    fake_codec::fail_volume_write = false;
    f.Send(Request());
    RODAK_CHECK_EQ(f.Receipt(1), rejected);
    RODAK_CHECK_EQ(fake_codec::volume_writes, writes);
    RODAK_CHECK_EQ(f.output.volume(), 60);
}

RODAK_TEST("MQTT service serializes an in-progress codec commit before Stop cancels the epoch") {
    Fixture f;
    RODAK_CHECK(f.output.OpenForOwner("test", 16000, 1, 16));
    f.Start();
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool release = false;
    fake_codec::before_volume_write = [&]() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&]() { return release; });
    };
    f.Send(Request());
    bool writing = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        writing = changed.wait_for(lock, std::chrono::seconds(3), [&]() { return entered; });
    }
    std::atomic<bool> stopped{false};
    std::thread stop([&]() { f.service.Stop(); stopped = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool waited = !stopped;
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
        changed.notify_all();
    }
    stop.join();
    fake_codec::before_volume_write = {};
    RODAK_CHECK(writing);
    RODAK_CHECK(waited);
    RODAK_CHECK_EQ(f.output.volume(), 30);
    RODAK_CHECK_EQ(fake_codec::volume_writes, 2);
}

RODAK_TEST("MQTT service Stop discards a pending receipt before destroying the SDK client") {
    Fixture f;
    f.Start();
    HoldUserEvents(true);
    f.Send(Request());
    RODAK_CHECK(WaitUntil([]() { return PendingUserEvents() != 0; }));
    RODAK_CHECK_EQ(f.output.volume(), 30);
    f.service.Stop();
    HoldUserEvents(false);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
    f.Start();
    f.Send(Request());
    auto replay = Parse(f.Receipt());
    RODAK_CHECK_EQ(Get(Get(replay.get(), "receipt"), "configurationRevision")->valueint, 1);
}

RODAK_TEST("MQTT service revokes volume authority as soon as refreshed configuration is unbound") {
    Fixture f;
    f.Start();
    HoldUserEvents(true);
    f.Send(Request());
    RODAK_CHECK(WaitUntil([]() { return PendingUserEvents() != 0; }));
    Disconnect();
    auto config = Config();
    config.unbind_pending = true;
    config.has_mqtt_config = false;
    SetConfig(config);
    RejectCredentials();
    // worker 的 credential refresh 完成后才处理这个命令；它不涉及音量权限。
    Connect();
    HoldUserEvents(false);
    f.Barrier();
    f.Send(Request("after-unbind", 70, 2));
    f.Barrier();
    RODAK_CHECK_EQ(f.output.volume(), 30);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
}

RODAK_TEST("MQTT service bounds unsafe JSON before the legacy desired parser") {
    Fixture f;
    f.Start();
    f.Send("{\"desired\":{\"volume\":20},\"extra\":" + std::string(64, '[') + "0" +
           std::string(64, ']') + "}");
    f.Send(R"({"desired":{"volume":20},"_meta":{"rodak/deviceEffect\u0000":{}}})");
    f.Barrier();
    RODAK_CHECK_EQ(f.output.volume(), 60);
    RODAK_CHECK_EQ(ReceiptCount(), 0u);
}
