#include "test_framework.h"
#include "host_runtime.h"
#include "phone_os/display_service.h"
#include "phone_os/display_control_ack_tracker.h"
#include "freertos/task.h"
#include "esp_peer.h"
#include <cJSON.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    esp_peer_handle_t peer = nullptr;
    uint16_t stream_id = 0;

    Fixture() { host::Reset(); }
    ~Fixture() {
        host::ReleaseSend();
        service.Stop();
        host::JoinTasks();
    }
    bool TryStart(uint16_t stream = 11) {
        if (!service.Start({}, [](auto, auto&&) {}, {},
                [this](const std::string& payload, Service::ControlReply reply) {
                    if (!payload.empty()) replies.push_back(std::move(reply));
                })) return false;
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

RODAK_TEST("display ACK transport failures are returned and never replayed") {
    for (const auto failure : {ESP_PEER_ERR_FAIL, ESP_PEER_ERR_WOULD_BLOCK}) {
        Fixture fixture;
        fixture.Start();
        fixture.Receive(1);
        fixture.Reply(0);
        const auto batch = fixture.service.control_acks_->Take();
        RODAK_CHECK_EQ(batch.size(), 1u);
        host::SetSendResult(failure);
        RODAK_CHECK_FALSE(fixture.service.SendControlAck(batch[0]));
        fixture.Receive(2);
        fixture.Reply(1);
        fixture.service.FlushControlAcks();
        RODAK_CHECK_EQ(host::SentFrames().size(), 2u);
        fixture.service.FlushControlAcks();
        RODAK_CHECK_EQ(host::SentFrames().size(), 2u);
        fixture.Stop();
        fixture.Start(47);
        host::SetSendResult(ESP_PEER_ERR_NONE);
        fixture.service.FlushControlAcks();
        RODAK_CHECK_EQ(host::SentFrames().size(), 2u);
    }
}

RODAK_TEST("display ACK all six startup resource failures leave no live instance") {
    Fixture fixture;
    fixture.display.capture_allowed = false;
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    fixture.display.capture_allowed = true;
    host::SetDefaultImplAvailable(false);
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    host::SetDefaultImplAvailable(true);
    host::SetOpenResult(ESP_PEER_ERR_FAIL);
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    host::SetOpenResult(ESP_PEER_ERR_NONE);
    host::SetConnectionResult(ESP_PEER_ERR_FAIL);
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    host::SetConnectionResult(ESP_PEER_ERR_NONE);
    host::SetTaskCreationAllowed(false);
    RODAK_CHECK_FALSE(fixture.TryStart());
    RODAK_CHECK_FALSE(fixture.service.control_acks_->Current());
    host::SetTaskCreationAllowed(true);
    fixture.display.jpeg_allowed = false;
    RODAK_CHECK_FALSE(fixture.TryStart());
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
