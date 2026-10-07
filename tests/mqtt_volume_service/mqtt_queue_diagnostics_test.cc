#include "host_runtime.h"
#include "phone_os/battery_monitor.h"
#include "phone_os/light_service.h"
#include "phone_os/mqtt_credential_refresh_policy.h"
#include "phone_os/mqtt_volume_effect.h"
#include "phone_os/mqtt_light_effect.h"
#include "phone_os/mqtt_command_ledger.h"
#include "phone_os/stream_lease.h"
#include <array>
#include <memory>
#include <utility>

// 与现有 lifecycle 测试相同，仅当前 test TU 读取真实状态和类型。
#define private public
#include "phone_os/unified_mqtt_service.h"
#undef private
#include "service_fixture.h"
#include "allocation_probe.h"
#include "diagnostics_support.h"

#include <new>
#include <regex>

using namespace mqtt_host;
using Mqtt = rodakos::UnifiedMqttService;

namespace {
std::string Topic(const std::string& id) { return "devices/test-device/commands/" + id; }
void Process(const std::string& id) {
    Message(Topic(id), "ping");
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
}
void Drain() {
    esp_mqtt_event_t event;
    event.event_id = MQTT_USER_EVENT;
    Deliver(event);
}
std::vector<Publication> DiagnosticWire() {
    std::vector<Publication> result;
    for (const auto& item : WirePublications())
        if (item.topic.find("/commands/diag-") != std::string::npos) {
            RODAK_CHECK_FALSE(item.via_outbox);
            result.push_back(item);
        }
    return result;
}
std::string SingleLog(char level, const std::string& expected) {
    const auto logs = DiagnosticLogs();
    RODAK_CHECK_EQ(logs.size(), size_t{1});
    RODAK_CHECK_EQ(logs[0].level, level);
    std::cout << "[DIAGNOSTIC] " << logs[0].message << '\n';
    RODAK_CHECK(logs[0].message.find(expected) != std::string::npos);
    return logs[0].message;
}
size_t Field(const std::string& log, const std::string& name) {
    std::smatch match;
    RODAK_CHECK(std::regex_search(log, match, std::regex("(?:^| )" + name + "=([0-9]+)(?: |$)")));
    return std::stoull(match[1]);
}
void CheckInbound(const std::string& reason, const std::string& topic, size_t payload,
                  size_t depth, int64_t before, int64_t after) {
    const auto log = SingleLog('E', "reason=" + reason + " ");
    RODAK_CHECK(log.find(topic) == std::string::npos);
    RODAK_CHECK(log.find("host-only-payload-sentinel") == std::string::npos);
    RODAK_CHECK(log.find("{\"command\"") == std::string::npos);
    RODAK_CHECK_EQ(Field(log, "topic_bytes"), topic.size());
    RODAK_CHECK_EQ(Field(log, "payload_bytes"), payload);
    RODAK_CHECK_EQ(Field(log, "object_bytes"), sizeof(Mqtt::PendingMessage));
    RODAK_CHECK_EQ(Field(log, "queue_depth_sample"), depth);
    const auto sample = static_cast<int64_t>(Field(log, "at_us_sample"));
    RODAK_CHECK(sample >= before && sample <= after);
    // 仅证明真实日志参数选择 DEFAULT 查询；数值来自 host fake，不是设备堆证据。
    RODAK_CHECK_EQ(Field(log, "default_free"), size_t{131072});
    RODAK_CHECK_EQ(Field(log, "default_largest"), size_t{16384});
}
void CheckReleased(size_t slot, const char* owner) {
    const auto watch = ReadDeleteProbe(slot);
    RODAK_CHECK(watch.original_pointer != 0);
    RODAK_CHECK_EQ(watch.releases, 1u);
    RODAK_CHECK_FALSE(watch.live);
    std::cout << "[OWNER] " << owner << " bytes=" << watch.bytes
              << " releases=" << watch.releases << " live=" << watch.live << '\n';
}
struct ProbeGuard {
    ProbeGuard() { ResetAllocationProbes(); }
    ~ProbeGuard() { DisarmPendingAllocation(); }
};
void BeginFragment(Fixture& fixture, const std::string& topic, const std::string& payload) {
    const size_t middle = payload.size() / 2;
    Fragment(topic, payload.substr(0, middle), 0, static_cast<int>(payload.size()));
    std::lock_guard<std::mutex> lock(fixture.service.mqtt_mutex_);
    const auto& assembly = fixture.service.message_assembly_;
    RODAK_CHECK(assembly.active);
    RODAK_CHECK(assembly.topic.size() > 32 && assembly.payload.size() > 32);
    RODAK_CHECK(assembly.payload.capacity() >= payload.size());
    WatchDelete(0, assembly.topic.data(), assembly.topic.capacity() + 1);
    WatchDelete(1, assembly.payload.data(), assembly.payload.capacity() + 1);
}
void FinishFragment(const std::string& payload, bool fail) {
    const int middle = static_cast<int>(payload.size() / 2);
    const std::string tail = payload.substr(static_cast<size_t>(middle));
    ArmPendingAllocation(sizeof(Mqtt::PendingMessage), fail);
    Fragment("", tail, middle, static_cast<int>(payload.size()));
    DisarmPendingAllocation();
    const auto probe = PendingAllocationProbe();
    RODAK_CHECK_EQ(probe.attempts, size_t{1});
    RODAK_CHECK_EQ(probe.unexpected_size_calls, size_t{0});
    RODAK_CHECK_EQ(probe.matched_size, sizeof(Mqtt::PendingMessage));
    RODAK_CHECK_EQ(probe.failures, fail ? size_t{1} : size_t{0});
}
std::string LongPing() {
    return "{\"command\":\"ping\",\"padding\":\"host-only-payload-sentinel-" + std::string(240, 'x') + "\"}";
}
void FillInbound(Fixture& fixture) {
    PauseDequeue(true);
    // 停车项仍经真实 handler，但不生成第九个出站 ACK 混淆另一个 8 槽预算。
    Message("host/diag-inflight", "ignored");
    RODAK_CHECK(WaitDequeued());
    for (int index = 0; index < 8; ++index) {
        Message(Topic("diag-fifo-" + std::to_string(index)), "ping");
        const auto queue = ReadQueue(fixture.service.message_queue_);
        RODAK_CHECK_EQ(queue.depth, static_cast<size_t>(index + 1));
        RODAK_CHECK_EQ(queue.last_send_wait, TickType_t{0});
    }
    RODAK_CHECK_EQ(ReadQueue(fixture.service.message_queue_).capacity, size_t{8});
}
Mqtt::CommandPublishContext Context(Fixture& fixture, const std::string& id) {
    std::lock_guard<std::mutex> lock(fixture.service.mqtt_mutex_);
    return {fixture.service.client_generation_, fixture.service.connection_epoch_, Topic(id) + "/ack"};
}
void CheckOutbound(const std::string& reason, size_t count, size_t bytes, size_t request) {
    const auto log = SingleLog('W', "reason=" + reason + " ");
    RODAK_CHECK_EQ(Field(log, "publication_count"), count);
    RODAK_CHECK_EQ(Field(log, "publication_bytes"), bytes);
    RODAK_CHECK_EQ(Field(log, "request_bytes"), request);
}
}

RODAK_TEST("MQTT diagnostics exact scalar nothrow failure leaves string allocation available") {
    ProbeGuard guard;
    ArmPendingAllocation(sizeof(Mqtt::PendingMessage), true);
    void* normal = ::operator new(sizeof(Mqtt::PendingMessage));
    void* array = ::operator new[](sizeof(Mqtt::PendingMessage), std::nothrow);
    const std::string text(sizeof(Mqtt::PendingMessage), 'x');
    RODAK_CHECK(normal != nullptr && array != nullptr && text.size() == sizeof(Mqtt::PendingMessage));
    RODAK_CHECK_EQ(PendingAllocationProbe().attempts, size_t{0});
    DisarmPendingAllocation();
    ::operator delete(normal);
    ::operator delete[](array);
}

RODAK_TEST("MQTT diagnostics object allocation failure never sends and frees both assembled strings") {
    ProbeGuard guard;
    Fixture fixture;
    fixture.Start();
    const auto topic = Topic("diag-allocation-fails");
    const auto payload = LongPing();
    BeginFragment(fixture, topic, payload);
    const auto queue = ReadQueue(fixture.service.message_queue_);
    const auto before = esp_timer_get_time();
    FinishFragment(payload, true);
    const auto after = esp_timer_get_time();
    const auto failed = ReadQueue(fixture.service.message_queue_);
    RODAK_CHECK_EQ(failed.send_attempts, queue.send_attempts);
    RODAK_CHECK_EQ(failed.send_accepted, queue.send_accepted);
    RODAK_CHECK_EQ(failed.depth, size_t{0});
    CheckReleased(0, "allocation-failure-topic");
    CheckReleased(1, "allocation-failure-payload");
    RODAK_CHECK_EQ(ReadDeleteProbe(2).original_pointer, uintptr_t{0});
    CheckInbound("object_alloc_failed", topic, payload.size(), 0, before, after);
    ResetDiagnosticLogs();
    Process("diag-allocation-next");
    RODAK_CHECK(WaitUntil([]() { return DiagnosticWire().size() == 1; }));
    RODAK_CHECK_EQ(DiagnosticWire()[0].topic, Topic("diag-allocation-next") + "/ack");
    RODAK_CHECK(DiagnosticLogs().empty());
}

RODAK_TEST("MQTT diagnostics real eight slot queue rejects ninth and preserves FIFO then recovers") {
    ProbeGuard guard;
    Fixture fixture;
    fixture.Start();
    FillInbound(fixture);
    const auto topic = Topic("diag-ninth-rejected");
    const auto payload = LongPing();
    BeginFragment(fixture, topic, payload);
    const auto queue = ReadQueue(fixture.service.message_queue_);
    const auto before = esp_timer_get_time();
    FinishFragment(payload, false);
    const auto after = esp_timer_get_time();
    const auto rejected = ReadQueue(fixture.service.message_queue_);
    RODAK_CHECK_EQ(rejected.send_attempts, queue.send_attempts + 1);
    RODAK_CHECK_EQ(rejected.send_accepted, queue.send_accepted);
    RODAK_CHECK_EQ(rejected.depth, size_t{8});
    RODAK_CHECK_EQ(rejected.last_send_wait, TickType_t{0});
    CheckReleased(0, "queue-rejection-topic");
    CheckReleased(1, "queue-rejection-payload");
    CheckReleased(2, "queue-rejection-object");
    CheckInbound("queue_send_rejected", topic, payload.size(), 8, before, after);
    ResetDiagnosticLogs();
    PauseDequeue(false);
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
    RODAK_CHECK(WaitUntil([]() { return DiagnosticWire().size() == 8; }));
    const auto delivered = DiagnosticWire();
    for (size_t index = 0; index < 8; ++index)
        RODAK_CHECK_EQ(delivered[index].topic, Topic("diag-fifo-" + std::to_string(index)) + "/ack");
    Process("diag-after-drain");
    RODAK_CHECK(WaitUntil([]() { return DiagnosticWire().size() == 9; }));
    RODAK_CHECK_EQ(DiagnosticWire().back().topic, Topic("diag-after-drain") + "/ack");
    RODAK_CHECK(DiagnosticLogs().empty());
}

RODAK_TEST("MQTT diagnostics queue reason survives a real drain before depth sample") {
    Fixture fixture;
    fixture.Start();
    FillInbound(fixture);
    bool drained = false;
    BeforeNextQueueDepthSample([](QueueHandle_t queue, void* context) {
        PauseDequeue(false);
        *static_cast<bool*>(context) = WaitUntil([&]() { return ReadQueue(queue).depth < 8; });
    }, &drained);
    Message(Topic("diag-sample-drain-rejected"), "ping");
    RODAK_CHECK(drained);
    const auto log = SingleLog('E', "reason=queue_send_rejected ");
    RODAK_CHECK(Field(log, "queue_depth_sample") < 8);
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
    RODAK_CHECK(WaitUntil([]() { return DiagnosticWire().size() == 8; }));
    for (const auto& sent : DiagnosticWire())
        RODAK_CHECK_NE(sent.topic, Topic("diag-sample-drain-rejected") + "/ack");
}

RODAK_TEST("MQTT diagnostics successful admission has no drop log and releases worker owners") {
    ProbeGuard guard;
    Fixture fixture;
    fixture.Start();
    const auto topic = Topic("diag-success-no-drop");
    const auto payload = LongPing();
    BeginFragment(fixture, topic, payload);
    const auto before = ReadQueue(fixture.service.message_queue_);
    FinishFragment(payload, false);
    RODAK_CHECK(WaitWorkerProcessed(LastQueuedMessage()));
    RODAK_CHECK(WaitUntil([]() { return DiagnosticWire().size() == 1; }));
    const auto after = ReadQueue(fixture.service.message_queue_);
    RODAK_CHECK_EQ(after.send_attempts, before.send_attempts + 1);
    RODAK_CHECK_EQ(after.send_accepted, before.send_accepted + 1);
    RODAK_CHECK_EQ(after.last_send_wait, TickType_t{0});
    CheckReleased(0, "success-topic");
    CheckReleased(1, "success-payload");
    CheckReleased(2, "success-object");
    RODAK_CHECK(DiagnosticLogs().empty());
}

RODAK_TEST("MQTT diagnostics outbound count limit retains eight ordered outputs and recovers") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    for (int index = 0; index < 8; ++index) Process("diag-output-" + std::to_string(index));
    size_t bytes = 0;
    {
        std::lock_guard<std::mutex> lock(fixture.service.mqtt_mutex_);
        RODAK_CHECK_EQ(fixture.service.command_publications_.size(), size_t{8});
        for (const auto& publication : fixture.service.command_publications_) bytes += publication.payload.size();
        RODAK_CHECK_EQ(fixture.service.command_publication_bytes_, bytes);
    }
    RODAK_CHECK_FALSE(fixture.service.QueueCommandPublication(Context(fixture, "diag-output-reject"), "ninth"));
    CheckOutbound("count_limit", 8, bytes, 5);
    Drain();
    const auto wire = DiagnosticWire();
    RODAK_CHECK_EQ(wire.size(), size_t{8});
    for (size_t index = 0; index < 8; ++index)
        RODAK_CHECK_EQ(wire[index].topic, Topic("diag-output-" + std::to_string(index)) + "/ack");
    ResetDiagnosticLogs();
    Process("diag-output-after-drain");
    Drain();
    RODAK_CHECK_EQ(DiagnosticWire().size(), size_t{9});
    RODAK_CHECK(DiagnosticLogs().empty());
}

RODAK_TEST("MQTT diagnostics outbound exact byte budget rejects only overflow and releases budget") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    const std::string first(64 * 1024, 'a');
    const std::string second(64 * 1024, 'b');
    RODAK_CHECK(fixture.service.QueueCommandPublication(Context(fixture, "diag-bytes-a"), first));
    RODAK_CHECK(fixture.service.QueueCommandPublication(Context(fixture, "diag-bytes-b"), second));
    RODAK_CHECK(DiagnosticLogs().empty());
    RODAK_CHECK_FALSE(fixture.service.QueueCommandPublication(Context(fixture, "diag-bytes-reject"), "x"));
    CheckOutbound("byte_limit", 2, 128 * 1024, 1);
    Drain();
    auto sent = DiagnosticWire();
    RODAK_CHECK_EQ(sent.size(), size_t{2});
    RODAK_CHECK_EQ(sent[0].payload, first);
    RODAK_CHECK_EQ(sent[1].payload, second);
    ResetDiagnosticLogs();
    RODAK_CHECK(fixture.service.QueueCommandPublication(Context(fixture, "diag-bytes-after"), first));
    Drain();
    RODAK_CHECK_EQ(DiagnosticWire().size(), size_t{3});
    RODAK_CHECK(DiagnosticLogs().empty());
}

RODAK_TEST("MQTT diagnostics outbound count reason takes precedence when both budgets are full") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    const std::string payload(16 * 1024, 'c');
    for (int index = 0; index < 8; ++index)
        RODAK_CHECK(fixture.service.QueueCommandPublication(Context(fixture, "diag-both-" + std::to_string(index)), payload));
    RODAK_CHECK_FALSE(fixture.service.QueueCommandPublication(Context(fixture, "diag-both-reject"), "x"));
    CheckOutbound("count_limit", 8, 128 * 1024, 1);
    Drain();
    RODAK_CHECK_EQ(DiagnosticWire().size(), size_t{8});
}

RODAK_TEST("MQTT diagnostics single payload limit remains separate from queued byte pressure") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    RODAK_CHECK_FALSE(fixture.service.QueueCommandPublication(Context(fixture, "diag-oversize"), std::string(64 * 1024 + 1, 'x')));
    SingleLog('W', "topic or payload exceeds the publication limit");
    RODAK_CHECK_EQ(PendingUserEvents(), size_t{0});
    ResetDiagnosticLogs();
    RODAK_CHECK(fixture.service.QueueCommandPublication(Context(fixture, "diag-maximum"), std::string(64 * 1024, 'm')));
    Drain();
    RODAK_CHECK_EQ(DiagnosticWire().size(), size_t{1});
    RODAK_CHECK(DiagnosticLogs().empty());
}

RODAK_TEST("MQTT diagnostics epoch change discards old output and restores both queue budgets") {
    Fixture fixture;
    fixture.Start();
    HoldUserEvents(true);
    const auto old = Context(fixture, "diag-old-epoch");
    const std::string payload(64 * 1024, 'e');
    RODAK_CHECK(fixture.service.QueueCommandPublication(old, payload));
    RODAK_CHECK(fixture.service.QueueCommandPublication(old, payload));
    Disconnect();
    Connect();
    RODAK_CHECK_FALSE(fixture.service.QueueCommandPublication(old, "late"));
    SingleLog('W', "original MQTT connection is no longer current");
    ResetDiagnosticLogs();
    RODAK_CHECK(fixture.service.QueueCommandPublication(Context(fixture, "diag-current-epoch"), payload));
    Drain();
    RODAK_CHECK_EQ(DiagnosticWire().size(), size_t{1});
    RODAK_CHECK_EQ(DiagnosticWire()[0].topic, Topic("diag-current-epoch") + "/ack");
    RODAK_CHECK(DiagnosticLogs().empty());
}

RODAK_TEST("MQTT diagnostics Stop releases a queued message and all retained owners") {
    ProbeGuard guard;
    Fixture fixture;
    fixture.Start();
    PauseDequeue(true);
    Message(Topic("diag-stop-inflight"), "ping");
    RODAK_CHECK(WaitDequeued());
    const auto topic = Topic("diag-stop-queued");
    const auto payload = LongPing();
    BeginFragment(fixture, topic, payload);
    FinishFragment(payload, false);
    RODAK_CHECK_EQ(ReadQueue(fixture.service.message_queue_).depth, size_t{1});
    RODAK_CHECK(ReadDeleteProbe(2).live);
    std::thread stop([&]() { fixture.service.Stop(); });
    const bool stop_entered = WaitUntil([&]() { return !fixture.service.started_.load(); });
    PauseDequeue(false);
    stop.join();
    JoinWorkers();
    RODAK_CHECK(stop_entered);
    CheckReleased(0, "stop-topic");
    CheckReleased(1, "stop-payload");
    CheckReleased(2, "stop-object");
    for (const auto& item : DiagnosticWire()) RODAK_CHECK_NE(item.topic, topic + "/ack");
    RODAK_CHECK(DiagnosticLogs().empty());
}
