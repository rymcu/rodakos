#include "test_framework.h"
#include "host_runtime.h"
#include "phone_os/display_service.h"
#include "phone_os/display_control_ack_tracker.h"
#include "phone_os/remote_input_controller.h"
#include "freertos/task.h"
#include "esp_peer.h"
#include <cJSON.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {
thread_local bool reject_cpp_allocations = false;
thread_local bool watch_cpp_allocations = false;
struct AllocationWatch {
    std::atomic<void*> live{nullptr};
    void* address = nullptr;
    size_t bytes = 0;
    std::atomic<size_t> releases{0};
};
AllocationWatch allocation_watches[4];
size_t allocation_watch_count = 0;
void ObserveAllocation(void* value, size_t size) {
    if (!watch_cpp_allocations) return;
    if (allocation_watch_count >= 4) std::abort();
    auto& watch = allocation_watches[allocation_watch_count++];
    watch.address = value;
    watch.bytes = size;
    watch.releases.store(0);
    watch.live.store(value);
}
void ObserveRelease(void* value) {
    if (value == nullptr) return;
    for (auto& watch : allocation_watches) {
        void* expected = value;
        if (watch.live.compare_exchange_strong(expected, nullptr)) {
            watch.releases.fetch_add(1);
            break;
        }
    }
}
}
void* operator new(size_t size) {
    if (reject_cpp_allocations) throw std::bad_alloc();
    if (auto* value = std::malloc(size == 0 ? 1 : size)) {
        ObserveAllocation(value, size);
        return value;
    }
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { ObserveRelease(value); std::free(value); }
void operator delete(void* value, size_t) noexcept { ObserveRelease(value); std::free(value); }

// Only expose scheduling seams in this test TU. The production translation
// unit is separately compiled, unchanged, with its original private header.
#define private public
#include "phone_os/webrtc_display_service.h"
#undef private

namespace {
namespace host = rodakos_test::display_host;
using Service = rodakos::WebRtcDisplayService;

class Fixture {
public:
    rodakos::DisplayService display;
    Service service{&display};
    std::vector<Service::ControlReply> replies;
    Service::ControlCallback input_handler;
    rodakos::StreamLeasePtr lease;
    std::weak_ptr<rodakos::StreamLease> last_candidate;
    uint64_t next_lease_nonce = 0;
    esp_peer_handle_t peer = nullptr;
    uint16_t stream_id = 0;

    Fixture() { host::Reset(); }
    ~Fixture() {
        host::ReleaseSend();
        service.Stop();
        host::JoinTasks();
    }
    bool TryStart(uint16_t stream = 11) {
        Service::Config config;
        auto candidate = std::make_shared<rodakos::StreamLease>(1, 1, ++next_lease_nonce, "host-display");
        last_candidate = candidate;
        config.stream_lease = candidate;
        if (!service.Start(config, [](auto, auto&&) {}, {},
                [this](const std::string& payload, Service::ControlReply reply) {
                    if (input_handler) input_handler(payload, std::move(reply));
                    else if (!payload.empty()) replies.push_back(std::move(reply));
                })) return false;
        lease = std::move(candidate);
        peer = host::LatestPeer();
        stream_id = stream;
        host::OpenControlChannel(peer, stream_id);
        return true;
    }
    void Start(uint16_t stream = 11) { RODAK_CHECK(TryStart(stream)); }
    void Stop() { service.Stop(); host::JoinTasks(); }
    void Receive(uint32_t sequence, const std::string& kind = "text") {
        host::Receive(peer, stream_id,
            "{\"version\":1,\"seq\":" + std::to_string(sequence) +
            ",\"kind\":\"" + kind + "\",\"text\":\"hello\"}");
    }
    void Reply(size_t index, bool accepted = true, const char* reason = nullptr) {
        RODAK_CHECK(index < replies.size());
        replies[index](accepted, reason);
    }
};

void CheckFrame(const host::SentFrame& frame, esp_peer_handle_t peer, uint16_t stream,
                uint32_t sequence, bool accepted, const char* reason = nullptr) {
    RODAK_CHECK_EQ(frame.peer, peer);
    RODAK_CHECK_EQ(frame.stream_id, stream);
    RODAK_CHECK_EQ(frame.type, ESP_PEER_DATA_CHANNEL_DATA);
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLength(frame.payload.data(), frame.payload.size()), cJSON_Delete);
    RODAK_CHECK(cJSON_IsObject(root.get()));
    const auto* version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    const auto* seq = cJSON_GetObjectItemCaseSensitive(root.get(), "seq");
    const auto* result = cJSON_GetObjectItemCaseSensitive(root.get(), "accepted");
    const auto* encoded_reason = cJSON_GetObjectItemCaseSensitive(root.get(), "reason");
    RODAK_CHECK(cJSON_IsNumber(version));
    RODAK_CHECK_EQ(version->valuedouble, 1.0);
    RODAK_CHECK(cJSON_IsNumber(seq));
    RODAK_CHECK_EQ(seq->valuedouble, static_cast<double>(sequence));
    RODAK_CHECK(cJSON_IsBool(result));
    RODAK_CHECK_EQ(cJSON_IsTrue(result) != 0, accepted);
    if (reason) {
        RODAK_CHECK(cJSON_IsString(encoded_reason));
        RODAK_CHECK_EQ(std::string(encoded_reason->valuestring), std::string(reason));
    } else {
        RODAK_CHECK_EQ(encoded_reason, nullptr);
    }
}

bool WaitForStopRequest(Service& service) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::recursive_mutex> lock(service.mutex_);
            if (service.stop_requested_) return true;
        }
        std::this_thread::yield();
    }
    return false;
}
size_t json_allocations_left = 0;
void* FailJsonAllocation(size_t size) {
    if (json_allocations_left == 0) return nullptr;
    --json_allocations_left;
    return std::malloc(size);
}
struct JsonAllocationFailure {
    explicit JsonAllocationFailure(size_t successful_allocations) {
        json_allocations_left = successful_allocations;
        cJSON_Hooks hooks{FailJsonAllocation, std::free};
        cJSON_InitHooks(&hooks);
    }
    ~JsonAllocationFailure() { cJSON_InitHooks(nullptr); }
};
rodakos::StreamLeasePtr revoke_during_json;
size_t json_revocation_allocations = 0;
void* RevokeDuringJsonAllocation(size_t size) {
    ++json_revocation_allocations;
    if (revoke_during_json) revoke_during_json->Revoke();
    return std::malloc(size);
}
struct JsonLeaseRevocation {
    explicit JsonLeaseRevocation(rodakos::StreamLeasePtr lease) {
        revoke_during_json = std::move(lease);
        json_revocation_allocations = 0;
        cJSON_Hooks hooks{RevokeDuringJsonAllocation, std::free};
        cJSON_InitHooks(&hooks);
    }
    ~JsonLeaseRevocation() {
        cJSON_InitHooks(nullptr);
        revoke_during_json.reset();
    }
};
struct CppAllocationFailure {
    CppAllocationFailure() { reject_cpp_allocations = true; }
    ~CppAllocationFailure() { reject_cpp_allocations = false; }
};
void ResetAllocationWatches() {
    for (auto& watch : allocation_watches) {
        RODAK_CHECK_EQ(watch.live.load(), nullptr);
        watch.address = nullptr;
        watch.bytes = 0;
        watch.releases.store(0);
    }
    allocation_watch_count = 0;
}
struct WatchCppAllocation {
    WatchCppAllocation() { watch_cpp_allocations = true; }
    ~WatchCppAllocation() { watch_cpp_allocations = false; }
};
void PrintAllocationEvidence(const char* scenario) {
    for (size_t index = 0; index < allocation_watch_count; ++index) {
        const auto& watch = allocation_watches[index];
        std::cout << "JPEG allocation evidence: case=" << scenario << " index=" << index
                  << " pointer=" << watch.address << " bytes=" << watch.bytes
                  << " releases=" << watch.releases.load()
                  << " live=" << (watch.live.load() != nullptr) << '\n';
    }
}
void CheckVideo(const host::SentFrame& frame, esp_peer_handle_t peer, uint16_t stream,
                const std::vector<uint8_t>& bytes, int result = ESP_PEER_ERR_NONE) {
    RODAK_CHECK_EQ(frame.peer, peer);
    RODAK_CHECK_EQ(frame.stream_id, stream);
    RODAK_CHECK_EQ(frame.type, ESP_PEER_DATA_CHANNEL_DATA);
    RODAK_CHECK(frame.returned);
    RODAK_CHECK_EQ(frame.result, result);
    RODAK_CHECK_EQ(frame.payload.size(), bytes.size() + 5u);
    RODAK_CHECK_EQ(static_cast<uint8_t>(frame.payload[0]), 0x80u);
    const uint32_t size = (static_cast<uint32_t>(static_cast<uint8_t>(frame.payload[1])) << 24) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(frame.payload[2])) << 16) |
                         (static_cast<uint32_t>(static_cast<uint8_t>(frame.payload[3])) << 8) |
                         static_cast<uint8_t>(frame.payload[4]);
    RODAK_CHECK_EQ(size, bytes.size());
    RODAK_CHECK_EQ(frame.payload.substr(5), std::string(bytes.begin(), bytes.end()));
}
}

RODAK_TEST("display ACK encodes the actual admitted reply and control peer") {
    Fixture fixture;
    fixture.Start();
    fixture.Receive(1);
    fixture.Reply(0);
    RODAK_CHECK(host::SentFrames().empty());
    fixture.service.FlushControlAcks();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckFrame(frames[0], fixture.peer, 11, 1, true);
    fixture.service.FlushControlAcks();
    RODAK_CHECK_EQ(host::SentFrames().size(), 1u);
}

RODAK_TEST("display ACK encodes explicit and kind-specific rejection reasons") {
    Fixture fixture;
    fixture.Start(23);
    fixture.Receive(1);
    fixture.Reply(0, false);
    fixture.Receive(2, "key");
    fixture.Reply(1, false);
    fixture.Receive(3);
    fixture.Reply(2, false, "quoted \"reason\"\n换行");
    fixture.service.FlushControlAcks();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 3u);
    CheckFrame(frames[0], fixture.peer, 23, 1, false, "text_target_unavailable");
    CheckFrame(frames[1], fixture.peer, 23, 2, false, "control_disabled_or_invalid");
    CheckFrame(frames[2], fixture.peer, 23, 3, false, "quoted \"reason\"\n换行");
}

RODAK_TEST("display ACK drops delayed old replies across Stop Start and reused sequence") {
    Fixture fixture;
    fixture.Start(11);
    const auto old_peer = fixture.peer;
    fixture.Receive(1);
    fixture.Stop();
    fixture.Reply(0);
    fixture.Start(29);
    RODAK_CHECK_NE(fixture.peer, old_peer);
    fixture.Receive(1);
    fixture.Reply(0, false, "old_instance");
    fixture.Reply(1);
    fixture.service.FlushControlAcks();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckFrame(frames[0], fixture.peer, 29, 1, true);
    RODAK_CHECK(host::IsClosed(old_peer));
}

RODAK_TEST("display ACK clears queued old replies during Stop Start") {
    Fixture fixture;
    fixture.Start();
    fixture.Receive(1);
    fixture.Reply(0);
    fixture.Stop();
    fixture.Start(31);
    fixture.service.FlushControlAcks();
    RODAK_CHECK(host::SentFrames().empty());
    fixture.Receive(1);
    fixture.Reply(1);
    fixture.service.FlushControlAcks();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckFrame(frames[0], fixture.peer, 31, 1, true);
}

RODAK_TEST("display ACK rechecks instance for an already taken batch before a new peer") {
    Fixture fixture;
    fixture.Start();
    fixture.Receive(1);
    fixture.Reply(0);
    fixture.Receive(2);
    fixture.Reply(1, false, "old_batch");
    const auto old_batch = fixture.service.control_acks_->Take();
    RODAK_CHECK_EQ(old_batch.size(), 2u);
    fixture.Stop();
    fixture.Start(33);
    fixture.Receive(1);
    fixture.Reply(2);
    for (const auto& ack : old_batch) RODAK_CHECK_FALSE(fixture.service.SendControlAck(ack));
    RODAK_CHECK(host::SentFrames().empty());
    fixture.service.FlushControlAcks();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckFrame(frames[0], fixture.peer, 33, 1, true);
}

RODAK_TEST("display ACK waiting on peer lock is rejected after stop admission closes") {
    Fixture fixture;
    fixture.Start();
    fixture.Receive(1);
    fixture.Reply(0);
    const auto batch = fixture.service.control_acks_->Take();
    RODAK_CHECK_EQ(batch.size(), 1u);
    std::unique_lock<std::recursive_mutex> lock(fixture.service.peer_api_mutex_);
    std::atomic<bool> started{false};
    bool result = true;
    std::thread sender([&] {
        started = true;
        result = fixture.service.SendControlAck(batch[0]);
    });
    while (!started.load()) std::this_thread::yield();
    host::EmitState(fixture.peer, ESP_PEER_STATE_DISCONNECTED);
    lock.unlock();
    sender.join();
    RODAK_CHECK_FALSE(result);
    RODAK_CHECK(host::SentFrames().empty());
}

RODAK_TEST("display ACK admitted send may finish only on original peer before close") {
    Fixture fixture;
    fixture.Start(41);
    const auto old_peer = fixture.peer;
    fixture.Receive(1);
    fixture.Reply(0);
    host::BlockNextSend();
    std::thread sender([&] { fixture.service.FlushControlAcks(); });
    const bool blocked = host::WaitForBlockedSend();
    const bool could_take_send_lock = fixture.service.peer_api_mutex_.try_lock();
    if (could_take_send_lock) fixture.service.peer_api_mutex_.unlock();
    std::atomic<bool> stop_finished{false};
    std::thread stopper([&] {
        fixture.service.Stop();
        stop_finished = true;
    });
    const bool stop_requested = WaitForStopRequest(fixture.service);
    const bool cleanup_entered = host::WaitForCleanupEntry();
    const bool closed_during_send = host::IsClosed(old_peer);
    const bool finished_during_send = stop_finished.load();
    const bool restarted_during_send = fixture.TryStart(43);
    host::ReleaseSend();
    sender.join();
    stopper.join();
    if (restarted_during_send) fixture.service.Stop();
    host::JoinTasks();
    RODAK_CHECK(blocked);
    RODAK_CHECK_FALSE(could_take_send_lock);
    RODAK_CHECK(stop_requested);
    RODAK_CHECK(cleanup_entered);
    RODAK_CHECK_FALSE(closed_during_send);
    RODAK_CHECK_FALSE(finished_during_send);
    RODAK_CHECK_FALSE(restarted_during_send);
    RODAK_CHECK(host::IsClosed(old_peer));
    RODAK_CHECK_FALSE(host::CloseOverlappedSend());
    auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckFrame(frames[0], old_peer, 41, 1, true);
    fixture.Start(43);
    fixture.Receive(1);
    fixture.Reply(0, false, "old_after_send");
    fixture.Reply(1);
    fixture.service.FlushControlAcks();
    frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 2u);
    CheckFrame(frames[1], fixture.peer, 43, 1, true);
}

RODAK_TEST("display ACK terminal peer states invalidate delayed replies and taken batches") {
    for (const auto state : {ESP_PEER_STATE_CLOSED, ESP_PEER_STATE_DISCONNECTED,
            ESP_PEER_STATE_CONNECT_FAILED, ESP_PEER_STATE_DATA_CHANNEL_CLOSED,
            ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED}) {
        Fixture fixture;
        fixture.Start();
        fixture.Receive(1);
        fixture.Reply(0);
        const auto batch = fixture.service.control_acks_->Take();
        RODAK_CHECK_EQ(batch.size(), 1u);
        host::EmitState(fixture.peer, state);
        fixture.Reply(0);
        RODAK_CHECK_FALSE(fixture.service.SendControlAck(batch[0]));
        fixture.service.FlushControlAcks();
        RODAK_CHECK(host::SentFrames().empty());
    }
}

RODAK_TEST("display ACK control channel close prevents delivery") {
    Fixture fixture;
    fixture.Start();
    fixture.Receive(1);
    fixture.Reply(0);
    const auto batch = fixture.service.control_acks_->Take();
    RODAK_CHECK_EQ(batch.size(), 1u);
    host::CloseControlChannel(fixture.peer, fixture.stream_id);
    fixture.Reply(0);
    RODAK_CHECK_FALSE(fixture.service.SendControlAck(batch[0]));
    fixture.service.FlushControlAcks();
    RODAK_CHECK(host::SentFrames().empty());
}

RODAK_TEST("display ACK production validation encodes invalid JSON and replay rejection") {
    Fixture fixture;
    fixture.Start();
    host::Receive(fixture.peer, fixture.stream_id, "invalid");
    fixture.Receive(4);
    fixture.Reply(0);
    fixture.Receive(4);
    fixture.service.FlushControlAcks();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 3u);
    CheckFrame(frames[0], fixture.peer, 11, 0, false, "invalid_json");
    CheckFrame(frames[1], fixture.peer, 11, 4, true);
    CheckFrame(frames[2], fixture.peer, 11, 4, false, "invalid_or_replayed_sequence");
    RODAK_CHECK_EQ(fixture.replies.size(), 1u);
}

RODAK_TEST("display ACK temporary send pressure retries FIFO without repeating input") {
    for (const auto failure : {ESP_PEER_ERR_NO_MEM, ESP_PEER_ERR_WOULD_BLOCK}) {
        Fixture fixture;
        fixture.Start();
        fixture.Receive(1);
        fixture.Reply(0);
        host::SetSendResult(failure);
        fixture.service.FlushControlAcks();
        fixture.Receive(2);
        fixture.Reply(1, false, "local_touch_active");
        fixture.service.FlushControlAcks();
        RODAK_CHECK_EQ(host::SentFrames().size(), 2u);
        RODAK_CHECK_EQ(fixture.replies.size(), 2u);
        host::SetSendResult(ESP_PEER_ERR_NONE);
        fixture.service.FlushControlAcks();
        const auto frames = host::SentFrames();
        RODAK_CHECK_EQ(frames.size(), 4u);
        CheckFrame(frames[0], fixture.peer, 11, 1, true);
        CheckFrame(frames[1], fixture.peer, 11, 1, true);
        CheckFrame(frames[2], fixture.peer, 11, 1, true);
        CheckFrame(frames[3], fixture.peer, 11, 2, false, "local_touch_active");
        fixture.service.FlushControlAcks();
        RODAK_CHECK_EQ(host::SentFrames().size(), 4u);
    }
}

RODAK_TEST("display ACK retry cannot reach replacement peer or reused sequence") {
    Fixture fixture;
    fixture.Start();
    fixture.Receive(1);
    fixture.Reply(0);
    host::SetSendResult(ESP_PEER_ERR_WOULD_BLOCK);
    fixture.service.FlushControlAcks();
    const auto old_peer = fixture.peer;
    fixture.Stop();
    fixture.Start(47);
    host::SetSendResult(ESP_PEER_ERR_NONE);
    fixture.Reply(0);
    fixture.service.FlushControlAcks();
    RODAK_CHECK_EQ(host::SentFrames().size(), 1u);
    fixture.Receive(1);
    fixture.Reply(1);
    fixture.service.FlushControlAcks();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 2u);
    CheckFrame(frames[0], old_peer, 11, 1, true);
    CheckFrame(frames[1], fixture.peer, 47, 1, true);
}

RODAK_TEST("display ACK allocation failures never publish partial JSON and recover") {
    // Object, key/value fields, reason and JSON print buffer all fail in turn.
    for (size_t fail_after = 0; fail_after < 12; ++fail_after) {
        Fixture fixture;
        fixture.Start();
        fixture.Receive(1);
        fixture.Reply(0, false, "quoted \"reason\"\n换行");
        {
            JsonAllocationFailure failure(fail_after);
            fixture.service.FlushControlAcks();
        }
        RODAK_CHECK(host::SentFrames().empty());
        fixture.service.FlushControlAcks();
        const auto frames = host::SentFrames();
        RODAK_CHECK_EQ(frames.size(), 1u);
        CheckFrame(frames[0], fixture.peer, 11, 1, false, "quoted \"reason\"\n换行");
    }
}

RODAK_TEST("display ACK reason-copy OOM retains FIFO head for recovery or bounded close") {
    for (bool exhaust : {false, true}) {
        Fixture fixture;
        fixture.Start();
        fixture.Receive(1);
        const std::string reason(120, 'r');
        fixture.Reply(0, false, reason.c_str());
        {
            CppAllocationFailure failure;
            fixture.service.FlushControlAcks();
            if (exhaust) {
                host::AdvanceTimeUs(1000000);
                fixture.service.FlushControlAcks();
            }
        }
        RODAK_CHECK(host::SentFrames().empty());
        RODAK_CHECK_EQ(fixture.service.stop_requested_, exhaust);
        fixture.service.FlushControlAcks();
        const auto frames = host::SentFrames();
        RODAK_CHECK_EQ(frames.size(), exhaust ? 0u : 1u);
        if (!exhaust) CheckFrame(frames[0], fixture.peer, 11, 1, false, reason.c_str());
    }
}

RODAK_TEST("display ACK enqueue OOM fails closed without escaping the input callback") {
    Fixture fixture;
    fixture.Start();
    fixture.Receive(1);
    fixture.Reply(0);
    fixture.Receive(2);
    {
        CppAllocationFailure failure;
        fixture.Reply(1, false, "rejection reason beyond the inline string capacity");
    }
    fixture.service.FlushControlAcks();
    RODAK_CHECK(fixture.service.stop_requested_);
    RODAK_CHECK(host::SentFrames().empty());
    fixture.Stop();
    fixture.Start(59);
    fixture.Receive(1);
    fixture.Reply(2);
    fixture.service.FlushControlAcks();
    RODAK_CHECK_EQ(host::SentFrames().size(), 1u);
    CheckFrame(host::SentFrames()[0], fixture.peer, 59, 1, true);
}

RODAK_TEST("cancelled real input ACK allocation failure closes the original peer") {
    for (bool local_touch : {false, true}) {
        rodakos::RemoteInputController controller({});
        Fixture fixture;
        fixture.input_handler = [&](const std::string& payload, Service::ControlReply reply) {
            controller.Handle(fixture.lease, payload, std::move(reply));
        };
        fixture.Start();
        host::Receive(fixture.peer, fixture.stream_id,
            R"({"version":1,"seq":1,"kind":"control","action":"enable"})");
        fixture.service.FlushControlAcks();
        host::Receive(fixture.peer, fixture.stream_id,
            R"({"version":1,"seq":2,"kind":"pointer","action":"down","x":1,"y":1})");
        const std::string disable = R"({"version":1,"kind":"control","action":"disable"})";
        {
            CppAllocationFailure failure;
            if (local_touch) controller.OnLocalTouch();
            else controller.Handle(fixture.lease, disable, {});
        }
        fixture.service.FlushControlAcks();
        RODAK_CHECK(fixture.service.stop_requested_);
        RODAK_CHECK_EQ(host::SentFrames().size(), 1u);
        CheckFrame(host::SentFrames()[0], fixture.peer, 11, 1, true);
    }
}

RODAK_TEST("display ACK persistent pressure is bounded by time or attempts") {
    for (bool use_time : {false, true}) {
        Fixture fixture;
        fixture.Start();
        fixture.Receive(1);
        fixture.Reply(0);
        host::SetSendResult(ESP_PEER_ERR_WOULD_BLOCK);
        fixture.service.FlushControlAcks();
        if (use_time) host::AdvanceTimeUs(1000000);
        for (int i = 0; i < 50; ++i) fixture.service.FlushControlAcks();
        RODAK_CHECK(fixture.service.stop_requested_);
        RODAK_CHECK_EQ(host::SentFrames().size(), use_time ? 1u : 50u);
        host::SetSendResult(ESP_PEER_ERR_NONE);
        fixture.service.FlushControlAcks();
        RODAK_CHECK_EQ(host::SentFrames().size(), use_time ? 1u : 50u);
        fixture.Stop();
        fixture.Start(49);
        fixture.Receive(1);
        fixture.Reply(1);
        fixture.service.FlushControlAcks();
        CheckFrame(host::SentFrames().back(), fixture.peer, 49, 1, true);
    }
}

RODAK_TEST("display ACK fatal transport error and queue overflow close old control") {
    for (bool overflow : {false, true}) {
        Fixture fixture;
        fixture.Start();
        const size_t count = overflow ? 33 : 2;
        for (size_t i = 0; i < count; ++i) {
            fixture.Receive(i + 1);
            fixture.Reply(i);
        }
        host::SetSendResult(ESP_PEER_ERR_FAIL);
        fixture.service.FlushControlAcks();
        RODAK_CHECK(fixture.service.stop_requested_);
        RODAK_CHECK_EQ(host::SentFrames().size(), overflow ? 0u : 1u);
        host::SetSendResult(ESP_PEER_ERR_NONE);
        fixture.service.FlushControlAcks();
        RODAK_CHECK_EQ(host::SentFrames().size(), overflow ? 0u : 1u);
    }
}

RODAK_TEST("display ACK backpressure keeps production peer loop pumping and pauses JPEG") {
    Fixture fixture;
    fixture.Start();
    fixture.Receive(1);
    fixture.Reply(0);
    fixture.service.channel_open_ = true;
    fixture.service.pending_jpeg_ = {1, 2, 3};
    host::SetSendResult(ESP_PEER_ERR_WOULD_BLOCK);
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForMainLoops(4));
    host::EmitState(fixture.peer, ESP_PEER_STATE_DISCONNECTED);
    fixture.Stop();
    const auto frames = host::SentFrames();
    RODAK_CHECK(frames.size() >= 3u);
    for (const auto& frame : frames) CheckFrame(frame, fixture.peer, 11, 1, true);
    RODAK_CHECK_EQ(fixture.replies.size(), 1u);
    RODAK_CHECK(host::IsClosed(fixture.peer));
}

RODAK_TEST("display ACK all six startup resource failures leave no live instance") {
    Fixture fixture;
    fixture.display.capture_allowed = false;
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK(fixture.last_candidate.expired());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    fixture.display.capture_allowed = true;
    host::SetDefaultImplAvailable(false);
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK(fixture.last_candidate.expired());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    host::SetDefaultImplAvailable(true);
    host::SetOpenResult(ESP_PEER_ERR_FAIL);
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK(fixture.last_candidate.expired());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    host::SetOpenResult(ESP_PEER_ERR_NONE);
    host::SetConnectionResult(ESP_PEER_ERR_FAIL);
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK(fixture.last_candidate.expired());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    host::SetConnectionResult(ESP_PEER_ERR_NONE);
    host::SetTaskCreationAllowed(false);
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK(fixture.last_candidate.expired());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    host::SetTaskCreationAllowed(true);
    fixture.display.jpeg_allowed = false;
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK(fixture.last_candidate.expired());
    host::JoinTasks();
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    fixture.display.jpeg_allowed = true;
    fixture.Start();
    fixture.Receive(1);
    fixture.Reply(0);
    fixture.service.FlushControlAcks();
    RODAK_CHECK_EQ(host::SentFrames().size(), 1u);
}

RODAK_TEST("display ACK saved reply safely expires after full service destruction") {
    Service::ControlReply delayed;
    {
        Fixture fixture;
        fixture.Start();
        fixture.Receive(1);
        delayed = fixture.replies[0];
    }
    delayed(true, nullptr);
    RODAK_CHECK(host::SentFrames().empty());
    {
        Fixture fixture;
        fixture.Start(53);
        delayed(false, "expired");
        fixture.Receive(1);
        fixture.Reply(0);
        fixture.service.FlushControlAcks();
        const auto frames = host::SentFrames();
        RODAK_CHECK_EQ(frames.size(), 1u);
        CheckFrame(frames[0], fixture.peer, 53, 1, true);
    }
}

RODAK_TEST("peer timing separates real API wait from SDK work without bypassing its lock") {
    Fixture fixture;
    fixture.Start();
    host::SetMainLoopCostUs(200000);
    std::unique_lock<std::recursive_mutex> api_lock(fixture.service.peer_api_mutex_);
    const size_t reads = host::ClockReads();
    int result = ESP_PEER_ERR_FAIL;
    std::thread pump([&] { result = fixture.service.PumpPeer(fixture.peer); });
    const bool waiting = host::WaitForClockReads(reads + 3);
    host::AdvanceTimeUs(350000);
    api_lock.unlock();
    pump.join();
    RODAK_CHECK(waiting);
    RODAK_CHECK_EQ(result, ESP_PEER_ERR_NONE);
    const auto stats = fixture.service.loop_diagnostics_;
    RODAK_CHECK_EQ(stats.api_wait.samples, 1u);
    RODAK_CHECK(stats.api_wait.maximum_us >= 350000);
    RODAK_CHECK_EQ(stats.sdk.samples, 1u);
    RODAK_CHECK(stats.sdk.maximum_us >= 200000);
    RODAK_CHECK(stats.sdk.maximum_us < 300000);
    RODAK_CHECK(stats.api_wait.maximum_at_us < stats.sdk.maximum_at_us);
}

RODAK_TEST("peer timing separates service lock wait from API wait and SDK work") {
    Fixture fixture;
    fixture.Start();
    host::SetMainLoopCostUs(200000);
    std::unique_lock<std::recursive_mutex> service_lock(fixture.service.mutex_);
    const size_t reads = host::ClockReads();
    int result = ESP_PEER_ERR_FAIL;
    std::thread pump([&] { result = fixture.service.PumpPeer(fixture.peer); });
    const bool waiting = host::WaitForClockReads(reads + 1);
    host::AdvanceTimeUs(400000);
    service_lock.unlock();
    pump.join();
    RODAK_CHECK(waiting);
    RODAK_CHECK_EQ(result, ESP_PEER_ERR_NONE);
    const auto stats = fixture.service.loop_diagnostics_;
    RODAK_CHECK(stats.service_wait.maximum_us >= 400000);
    RODAK_CHECK(stats.api_wait.maximum_us < 100000);
    RODAK_CHECK(stats.sdk.maximum_us >= 200000);
    RODAK_CHECK(stats.sdk.maximum_us < 300000);
    RODAK_CHECK(stats.service_wait.maximum_at_us < stats.sdk.maximum_at_us);
}

RODAK_TEST("control timing begins before service lock and remains attached to parsed sequence") {
    Fixture fixture;
    fixture.Start(61);
    fixture.input_handler = [](const std::string& payload, Service::ControlReply reply) {
        if (!payload.empty()) host::AdvanceTimeUs(75000);
        reply(true, nullptr);
    };
    std::unique_lock<std::recursive_mutex> service_lock(fixture.service.mutex_);
    const size_t reads = host::ClockReads();
    std::thread input([&] { fixture.Receive(7); });
    const bool waiting = host::WaitForClockReads(reads + 1);
    host::AdvanceTimeUs(250000);
    service_lock.unlock();
    input.join();
    RODAK_CHECK(waiting);
    RODAK_CHECK_EQ(fixture.service.control_timing_count_, 1u);
    const auto sample = fixture.service.control_timings_[0];
    RODAK_CHECK_EQ(sample.sequence, 7u);
    RODAK_CHECK_EQ(sample.stream_id, 61u);
    RODAK_CHECK(sample.dispatch_us - sample.entered_us >= 250000);
    RODAK_CHECK(sample.returned_us - sample.dispatch_us >= 75000);
    RODAK_CHECK(fixture.service.loop_diagnostics_.service_wait.maximum_us >= 250000);
    fixture.service.FlushControlAcks();
    CheckFrame(host::SentFrames()[0], fixture.peer, 61, 7, true);
}

RODAK_TEST("diagnostic storage is bounded and ignores moves without changing admitted replies") {
    Fixture fixture;
    fixture.Start();
    for (uint32_t seq = 1; seq <= 20; ++seq) {
        fixture.Receive(seq);
        fixture.Reply(seq - 1);
    }
    RODAK_CHECK_EQ(fixture.service.control_timing_count_, 16u);
    RODAK_CHECK_EQ(fixture.service.control_timing_dropped_, 4u);
    host::Receive(fixture.peer, 11, R"({"version":1,"seq":21,"kind":"pointer","action":"move","x":1,"y":1})");
    fixture.Reply(20);
    RODAK_CHECK_EQ(fixture.service.control_timing_count_, 16u);
    RODAK_CHECK_EQ(fixture.service.control_timing_dropped_, 4u);
    fixture.service.FlushControlAcks();
    RODAK_CHECK_EQ(host::SentFrames().size(), 21u);
    // Periodic/final snapshot collection itself must not request C++ heap.
    {
        CppAllocationFailure failure;
        fixture.service.MaybeLogTimings(true);
    }
    RODAK_CHECK_EQ(fixture.service.control_timing_count_, 0u);
    RODAK_CHECK_EQ(fixture.service.control_timing_dropped_, 0u);
}

RODAK_TEST("JPEG timing includes nested SDK pumps while ACK timing preserves send result") {
    Fixture fixture;
    fixture.Start();
    fixture.service.channel_open_ = true;
    fixture.service.video_stream_id_ = 3;
    host::SetMainLoopCostUs(120000);
    host::SetSendCostUs(30000);
    RODAK_CHECK(fixture.service.SendJpegChunks(std::vector<uint8_t>(40001, 1)));
    auto stats = fixture.service.loop_diagnostics_;
    RODAK_CHECK_EQ(stats.jpeg.samples, 1u);
    RODAK_CHECK(stats.jpeg.maximum_us >= 330000);
    RODAK_CHECK_EQ(stats.sdk.samples, 2u);
    RODAK_CHECK(stats.sdk.total_us >= 240000);
    RODAK_CHECK_EQ(host::SentFrames().size(), 3u);
    fixture.Receive(1);
    fixture.Reply(0);
    fixture.service.FlushControlAcks();
    stats = fixture.service.loop_diagnostics_;
    RODAK_CHECK(stats.ack.maximum_us >= 30000);
    CheckFrame(host::SentFrames().back(), fixture.peer, 11, 1, true);
}

RODAK_TEST("control timing from replaced callback cannot populate the new peer generation") {
    Fixture fixture;
    fixture.Start();
    const auto first_generation = fixture.service.timing_generation_;
    bool replaced = false;
    fixture.input_handler = [&](const std::string& payload, Service::ControlReply reply) {
        if (!payload.empty() && !replaced) {
            replaced = true;
            fixture.Stop();
            fixture.Start(63);
        }
        reply(true, nullptr);
    };
    fixture.Receive(1);
    RODAK_CHECK(fixture.service.timing_generation_ > first_generation);
    RODAK_CHECK_EQ(fixture.service.control_timing_count_, 0u);
    fixture.service.FlushControlAcks();
    RODAK_CHECK(host::SentFrames().empty());
    fixture.Receive(1);
    fixture.service.FlushControlAcks();
    RODAK_CHECK_EQ(fixture.service.control_timing_count_, 1u);
    CheckFrame(host::SentFrames().back(), fixture.peer, 63, 1, true);
}

RODAK_TEST("production peer loop measures scheduling gaps and resets diagnostics on restart") {
    Fixture fixture;
    fixture.Start();
    host::SetMainLoopCostUs(1000);
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForMainLoops(4));
    Service::LoopDiagnostics snapshot;
    {
        std::lock_guard<std::recursive_mutex> lock(fixture.service.mutex_);
        snapshot = fixture.service.loop_diagnostics_;
    }
    RODAK_CHECK(snapshot.gap.samples >= 2u);
    RODAK_CHECK(snapshot.gap.maximum_us >= 1000);
    RODAK_CHECK(snapshot.sdk.samples >= 3u);
    fixture.Stop();
    fixture.Start(65);
    RODAK_CHECK_EQ(fixture.service.loop_diagnostics_.sdk.samples, 0u);
    RODAK_CHECK_EQ(fixture.service.loop_diagnostics_.gap.samples, 0u);
    RODAK_CHECK_EQ(fixture.service.control_timing_count_, 0u);
}

RODAK_TEST("preopen first JPEG waits for actual video open and production task sends it once") {
    Fixture fixture;
    fixture.Start();
    const std::vector<uint8_t> bytes{0xff, 0xd8, 1, 0xff, 0xd9};
    RODAK_CHECK(fixture.display.EmitJpeg(std::vector<uint8_t>(bytes), 1, 1234));
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForMainLoops(4));
    RODAK_CHECK(host::SentFrames().empty());
    host::OpenVideoChannel(fixture.peer, 5);
    const bool video_returned = host::WaitForVideoSendReturns(5, 1);
    RODAK_CHECK(host::WaitForMainLoops(host::MainLoopCount() + 4));
    fixture.Stop();
    std::cout << "preopen probe: received=" << fixture.service.stats_frames_received_.load()
              << " dropped=" << fixture.service.stats_frames_dropped_.load()
              << " SDK_complete_frames=" << fixture.service.stats_frames_sent_.load() << '\n';
    RODAK_CHECK(video_returned);
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckVideo(frames[0], fixture.peer, 5, bytes);
}

RODAK_TEST("postopen first JPEG still travels through the production task") {
    Fixture fixture;
    fixture.Start();
    host::OpenVideoChannel(fixture.peer, 7);
    host::RunPeerTasks();
    const std::vector<uint8_t> bytes{0xff, 0xd8, 2, 0xff, 0xd9};
    RODAK_CHECK(fixture.display.EmitJpeg(std::vector<uint8_t>(bytes), 1, 4567));
    RODAK_CHECK(host::WaitForVideoSendReturns(7, 1));
    RODAK_CHECK(host::WaitForMainLoops(host::MainLoopCount() + 4));
    fixture.Stop();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckVideo(frames[0], fixture.peer, 7, bytes);
}

RODAK_TEST("preopen latest JPEG moves without allocation and frees replaced storage") {
    Fixture fixture;
    fixture.Start();
    ResetAllocationWatches();
    std::vector<uint8_t> first;
    std::vector<uint8_t> latest;
    {
        WatchCppAllocation capture;
        first = std::vector<uint8_t>(4096, 1);
        latest = std::vector<uint8_t>(512, 2);
    }
    RODAK_CHECK_EQ(allocation_watch_count, 2u);
    RODAK_CHECK_EQ(allocation_watches[0].address, first.data());
    RODAK_CHECK_EQ(allocation_watches[1].address, latest.data());
    RODAK_CHECK_EQ(allocation_watches[0].bytes, 4096u);
    RODAK_CHECK_EQ(allocation_watches[1].bytes, 512u);
    {
        CppAllocationFailure failure;
        fixture.display.EmitJpeg(std::move(first), 1, 100);
        fixture.display.EmitJpeg(std::move(latest), 2, 200);
    }
    RODAK_CHECK_EQ(allocation_watches[0].releases.load(), 1u);
    RODAK_CHECK_EQ(allocation_watches[1].releases.load(), 0u);
    RODAK_CHECK_EQ(fixture.service.pending_jpeg_.data(), allocation_watches[1].address);
    RODAK_CHECK_EQ(fixture.service.pending_jpeg_sequence_, 2u);
    RODAK_CHECK_EQ(fixture.service.pending_jpeg_timestamp_us_, 200);
    RODAK_CHECK_EQ(fixture.service.stats_frames_dropped_.load(), 1u);
    host::OpenVideoChannel(fixture.peer, 9);
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForVideoSendReturns(9, 1));
    RODAK_CHECK(host::WaitForMainLoops(host::MainLoopCount() + 4));
    RODAK_CHECK_EQ(allocation_watches[1].releases.load(), 1u);
    fixture.Stop();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckVideo(frames[0], fixture.peer, 9, std::vector<uint8_t>(512, 2));
    PrintAllocationEvidence("latest-replacement");
}

RODAK_TEST("preopen JPEG cannot cross Stop Start with a reused frame sequence") {
    Fixture fixture;
    fixture.Start();
    const auto old_peer = fixture.peer;
    fixture.display.EmitJpeg({1, 1, 1}, 1, 100);
    fixture.Stop();
    RODAK_CHECK(fixture.service.pending_jpeg_.empty());
    RODAK_CHECK_EQ(fixture.service.pending_jpeg_.capacity(), 0u);
    RODAK_CHECK_FALSE(fixture.display.EmitJpeg({9}, 1, 111));
    fixture.Start(23);
    host::OpenVideoChannel(fixture.peer, 25);
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForMainLoops(host::MainLoopCount() + 4));
    RODAK_CHECK(host::SentFrames().empty());
    fixture.display.EmitJpeg({2, 2, 2}, 1, 200);
    RODAK_CHECK(host::WaitForVideoSendReturns(25, 1));
    fixture.Stop();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    RODAK_CHECK_NE(frames[0].peer, old_peer);
    CheckVideo(frames[0], fixture.peer, 25, {2, 2, 2});
}

RODAK_TEST("preopen terminal state revokes and discards JPEG without video dispatch") {
    Fixture fixture;
    fixture.Start();
    fixture.display.EmitJpeg({1, 2, 3}, 1, 100);
    host::EmitState(fixture.peer, ESP_PEER_STATE_DISCONNECTED);
    RODAK_CHECK_FALSE(fixture.lease->IsActive());
    host::OpenVideoChannel(fixture.peer, 27);
    fixture.display.EmitJpeg({4, 5, 6}, 2, 200);
    fixture.Stop();
    RODAK_CHECK(host::SentFrames().empty());
    RODAK_CHECK_EQ(fixture.service.pending_jpeg_.capacity(), 0u);
    RODAK_CHECK(host::IsClosed(fixture.peer));
}

RODAK_TEST("preopen SDK failure is only an attempt and does not replay without a new JPEG") {
    Fixture fixture;
    fixture.Start();
    fixture.display.EmitJpeg({1, 2, 3}, 1, 100);
    host::SetSendResult(ESP_PEER_ERR_FAIL);
    host::OpenVideoChannel(fixture.peer, 29);
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForVideoSendReturns(29, 1));
    RODAK_CHECK(host::WaitForMainLoops(host::MainLoopCount() + 4));
    RODAK_CHECK_EQ(host::SentFrames().size(), 1u);
    RODAK_CHECK_EQ(fixture.service.stats_frames_sent_.load(), 0u);
    RODAK_CHECK_EQ(fixture.service.stats_send_errors_.load(), 1u);
    host::SetSendResult(ESP_PEER_ERR_NONE);
    fixture.display.EmitJpeg({4, 5, 6}, 2, 200);
    RODAK_CHECK(host::WaitForVideoSendReturns(29, 2));
    fixture.Stop();
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 2u);
    CheckVideo(frames[0], fixture.peer, 29, {1, 2, 3}, ESP_PEER_ERR_FAIL);
    CheckVideo(frames[1], fixture.peer, 29, {4, 5, 6});
    RODAK_CHECK_EQ(fixture.service.stats_frames_sent_.load(), 1u);
}

RODAK_TEST("preopen JPEG remains pending while reliable ACK retries have priority") {
    Fixture fixture;
    fixture.Start(31);
    fixture.display.EmitJpeg({3, 1, 4}, 1, 100);
    fixture.Receive(1);
    fixture.Reply(0);
    host::SetSendResult(ESP_PEER_ERR_WOULD_BLOCK);
    host::OpenVideoChannel(fixture.peer, 33);
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForMainLoops(4));
    const auto pressured = host::SentFrames();
    RODAK_CHECK_FALSE(pressured.empty());
    for (const auto& attempt : pressured) {
        RODAK_CHECK_EQ(attempt.stream_id, 31u);
        RODAK_CHECK(attempt.returned);
        RODAK_CHECK_EQ(attempt.result, ESP_PEER_ERR_WOULD_BLOCK);
    }
    {
        std::lock_guard<std::recursive_mutex> lock(fixture.service.mutex_);
        RODAK_CHECK_FALSE(fixture.service.pending_jpeg_.empty());
    }
    host::SetSendResult(ESP_PEER_ERR_NONE);
    RODAK_CHECK(host::WaitForVideoSendReturns(33, 1));
    fixture.Stop();
    const auto frames = host::SentFrames();
    RODAK_CHECK(frames.size() >= 3u);
    CheckFrame(frames[frames.size() - 2], fixture.peer, 31, 1, true);
    RODAK_CHECK_EQ(frames[frames.size() - 2].result, ESP_PEER_ERR_NONE);
    CheckVideo(frames.back(), fixture.peer, 33, {3, 1, 4});
}

RODAK_TEST("preopen in-flight JPEG and pending replacement both release before Stop completes") {
    Fixture fixture;
    fixture.Start();
    ResetAllocationWatches();
    std::vector<uint8_t> first;
    std::vector<uint8_t> next;
    {
        WatchCppAllocation capture;
        first = std::vector<uint8_t>(4096, 4);
        next = std::vector<uint8_t>(256, 5);
    }
    fixture.display.EmitJpeg(std::move(first), 1, 100);
    host::OpenVideoChannel(fixture.peer, 35);
    host::BlockNextSend();
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForBlockedSend());
    RODAK_CHECK_EQ(allocation_watches[0].releases.load(), 0u);
    fixture.display.EmitJpeg(std::move(next), 2, 200);
    const auto old_peer = fixture.peer;
    std::atomic<bool> stopped{false};
    std::thread stopping([&] { fixture.service.Stop(); stopped.store(true); });
    const bool stop_requested = WaitForStopRequest(fixture.service);
    const bool stopped_early = stopped.load();
    const bool old_closed_early = host::IsClosed(old_peer);
    const bool replacement_started_early = fixture.TryStart(37);
    const auto released_before_send = allocation_watches[0].releases.load();
    const auto pending_released_before_send = allocation_watches[1].releases.load();
    host::ReleaseSend();
    stopping.join();
    host::JoinTasks();
    RODAK_CHECK(stop_requested);
    RODAK_CHECK_FALSE(stopped_early);
    RODAK_CHECK_FALSE(old_closed_early);
    RODAK_CHECK_FALSE(replacement_started_early);
    RODAK_CHECK_EQ(released_before_send, 0u);
    RODAK_CHECK_EQ(pending_released_before_send, 0u);
    RODAK_CHECK_EQ(allocation_watches[0].releases.load(), 1u);
    RODAK_CHECK_EQ(allocation_watches[1].releases.load(), 1u);
    RODAK_CHECK_EQ(fixture.service.pending_jpeg_.capacity(), 0u);
    RODAK_CHECK_FALSE(host::CloseOverlappedSend());
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckVideo(frames[0], old_peer, 35, std::vector<uint8_t>(4096, 4));
    PrintAllocationEvidence("in-flight-and-pending-stop");
    fixture.Start(37);
    host::OpenVideoChannel(fixture.peer, 39);
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForMainLoops(host::MainLoopCount() + 4));
    fixture.Stop();
    RODAK_CHECK_EQ(host::SentFrames().size(), 1u);
}

RODAK_TEST("preopen video open never enables remote input and lease revocation stays authoritative") {
    rodakos::RemoteInputController controller({});
    Fixture fixture;
    fixture.input_handler = [&](const std::string& payload, Service::ControlReply reply) {
        controller.Handle(fixture.lease, payload, std::move(reply));
    };
    fixture.Start(41);
    fixture.display.EmitJpeg({6, 2, 6}, 1, 100);
    host::OpenVideoChannel(fixture.peer, 43);
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForVideoSendReturns(43, 1));
    RODAK_CHECK_FALSE(controller.IsEnabled());
    host::Receive(fixture.peer, 41, R"({"version":1,"seq":1,"kind":"pointer","action":"down","x":1,"y":1})");
    fixture.Receive(2);
    RODAK_CHECK(host::WaitForSendReturns(3));
    const auto rejected = host::SentFrames();
    CheckFrame(rejected[1], fixture.peer, 41, 1, false, "control_disabled_or_invalid");
    CheckFrame(rejected[2], fixture.peer, 41, 2, false, "control_disabled_or_invalid");
    host::Receive(fixture.peer, 41, R"({"version":1,"seq":3,"kind":"control","action":"enable"})");
    RODAK_CHECK(host::WaitForSendReturns(4));
    RODAK_CHECK(controller.IsEnabled());
    fixture.lease->Revoke();
    RODAK_CHECK_FALSE(controller.IsEnabled());
    fixture.Stop();
    RODAK_CHECK_FALSE(controller.IsEnabled());
}

RODAK_TEST("preopen cancelled handshake returns actual JPEG allocation and zero capacity") {
    Fixture fixture;
    fixture.Start();
    ResetAllocationWatches();
    std::vector<uint8_t> jpeg;
    {
        WatchCppAllocation capture;
        jpeg = std::vector<uint8_t>(8192, 7);
    }
    RODAK_CHECK_EQ(allocation_watch_count, 1u);
    RODAK_CHECK_EQ(allocation_watches[0].bytes, 8192u);
    fixture.display.EmitJpeg(std::move(jpeg), 1, 100);
    RODAK_CHECK_EQ(fixture.service.pending_jpeg_.data(), allocation_watches[0].address);
    RODAK_CHECK_EQ(allocation_watches[0].releases.load(), 0u);
    fixture.Stop();
    RODAK_CHECK_EQ(allocation_watches[0].releases.load(), 1u);
    RODAK_CHECK_EQ(allocation_watches[0].live.load(), nullptr);
    RODAK_CHECK_EQ(fixture.service.pending_jpeg_.capacity(), 0u);
    RODAK_CHECK_FALSE(fixture.service.config_.stream_lease);
    fixture.lease.reset();
    RODAK_CHECK(fixture.last_candidate.expired());
    RODAK_CHECK(host::SentFrames().empty());
    PrintAllocationEvidence("cancel-before-open");
}

RODAK_TEST("display requires an active caller stream lease before creating resources") {
    Fixture fixture;
    Service::Config config;
    RODAK_CHECK_FALSE(fixture.service.Start(config, [](auto, auto&&) {}));
    RODAK_CHECK_FALSE(fixture.display.capture_running);
    RODAK_CHECK_EQ(host::LatestPeer(), nullptr);
    config.stream_lease = std::make_shared<rodakos::StreamLease>(1, 1, 1, "revoked");
    config.stream_lease->Revoke();
    RODAK_CHECK_FALSE(fixture.service.Start(config, [](auto, auto&&) {}));
    RODAK_CHECK_FALSE(fixture.display.capture_running);
    RODAK_CHECK_EQ(host::LatestPeer(), nullptr);
    fixture.Start();
    fixture.Stop();
}

RODAK_TEST("preopen JPEG callback rejects its revoked original lease") {
    Fixture fixture;
    fixture.Start();
    fixture.lease->Revoke();
    fixture.display.EmitJpeg({1, 2, 3}, 1, 100);
    RODAK_CHECK(fixture.service.stop_requested_);
    RODAK_CHECK(fixture.service.pending_jpeg_.empty());
    host::OpenVideoChannel(fixture.peer, 45);
    fixture.Stop();
    RODAK_CHECK(host::SentFrames().empty());
}

RODAK_TEST("preopen retained JPEG is not sent when lease expires before channel open") {
    Fixture fixture;
    fixture.Start();
    fixture.display.EmitJpeg({2, 7, 1}, 1, 100);
    RODAK_CHECK_FALSE(fixture.service.pending_jpeg_.empty());
    fixture.lease->Revoke();
    host::OpenVideoChannel(fixture.peer, 47);
    host::RunPeerTasks();
    const bool stopped = WaitForStopRequest(fixture.service);
    fixture.Stop();
    RODAK_CHECK(stopped);
    RODAK_CHECK(host::SentFrames().empty());
    RODAK_CHECK_EQ(fixture.service.stats_frames_sent_.load(), 0u);
    RODAK_CHECK_EQ(fixture.service.pending_jpeg_.capacity(), 0u);
}

RODAK_TEST("JPEG WOULD_BLOCK retry obtains a fresh final lease admission") {
    Fixture fixture;
    fixture.Start();
    const auto original = fixture.lease;
    fixture.display.EmitJpeg({1, 2, 3}, 1, 100);
    host::SetSendResult(ESP_PEER_ERR_WOULD_BLOCK);
    host::OnNextSendReturn([original] { original->Revoke(); });
    host::OpenVideoChannel(fixture.peer, 49);
    host::RunPeerTasks();
    const bool stopped = WaitForStopRequest(fixture.service);
    fixture.Stop();
    RODAK_CHECK(stopped);
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    CheckVideo(frames[0], fixture.peer, 49, {1, 2, 3}, ESP_PEER_ERR_WOULD_BLOCK);
    RODAK_CHECK_EQ(fixture.service.stats_frames_sent_.load(), 0u);
    RODAK_CHECK_EQ(fixture.service.stats_chunks_sent_.load(), 0u);
    RODAK_CHECK_EQ(fixture.service.stats_send_retries_.load(), 1u);
    RODAK_CHECK_EQ(fixture.service.stats_frames_dropped_.load(), 1u);
}

RODAK_TEST("JPEG next fragment rejects revoked lease while admitted SDK call may complete") {
    Fixture fixture;
    fixture.Start();
    const auto original = fixture.lease;
    fixture.display.EmitJpeg(std::vector<uint8_t>(20001, 3), 1, 100);
    host::OnNextSendReturn([original] { original->Revoke(); });
    host::OpenVideoChannel(fixture.peer, 51);
    host::RunPeerTasks();
    const bool stopped = WaitForStopRequest(fixture.service);
    fixture.Stop();
    RODAK_CHECK(stopped);
    const auto frames = host::SentFrames();
    RODAK_CHECK_EQ(frames.size(), 1u);
    RODAK_CHECK_EQ(frames[0].peer, fixture.peer);
    RODAK_CHECK_EQ(frames[0].stream_id, 51u);
    RODAK_CHECK(frames[0].returned);
    RODAK_CHECK_EQ(frames[0].result, ESP_PEER_ERR_NONE);
    RODAK_CHECK_EQ(static_cast<uint8_t>(frames[0].payload[0]), 0u);
    RODAK_CHECK_EQ(frames[0].payload.size(), 20005u);
    RODAK_CHECK_EQ(fixture.service.stats_chunks_sent_.load(), 1u);
    RODAK_CHECK_EQ(fixture.service.stats_bytes_sent_.load(), 20000u);
    RODAK_CHECK_EQ(fixture.service.stats_frames_sent_.load(), 0u);
    RODAK_CHECK_EQ(fixture.service.stats_frames_dropped_.load(), 1u);
}

RODAK_TEST("production peer ACK rejects lease revoked while the API lock is unavailable") {
    Fixture fixture;
    fixture.Start(53);
    fixture.Receive(1);
    fixture.Reply(0);
    std::unique_lock<std::recursive_mutex> lock(fixture.service.peer_api_mutex_);
    const size_t reads = host::ClockReads();
    host::RunPeerTasks();
    const bool reached_peer = host::WaitForClockReads(reads + 3);
    fixture.lease->Revoke();
    lock.unlock();
    const bool stopped = WaitForStopRequest(fixture.service);
    fixture.Stop();
    RODAK_CHECK(reached_peer);
    RODAK_CHECK(stopped);
    RODAK_CHECK(host::SentFrames().empty());
}

RODAK_TEST("display ACK revocation during successful JSON encoding prevents SDK entry") {
    Fixture fixture;
    fixture.Start(55);
    fixture.Receive(1);
    fixture.Reply(0);
    RODAK_CHECK(fixture.lease->IsActive());
    {
        JsonLeaseRevocation revoke(fixture.lease);
        fixture.service.FlushControlAcks();
    }
    RODAK_CHECK(json_revocation_allocations > 1u);
    RODAK_CHECK_FALSE(fixture.lease->IsActive());
    RODAK_CHECK(host::SentFrames().empty());
    RODAK_CHECK(fixture.service.stop_requested_);
    fixture.Stop();
}
