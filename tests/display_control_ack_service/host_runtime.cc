#include "host_runtime.h"
#include "freertos/task.h"
#include "esp_peer_default.h"

#include <chrono>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace {
struct Task {
    std::thread thread;
    bool ready = false;
};
struct Peer {
    esp_peer_cfg_t config{};
    bool closed = false;
    bool sending = false;
};
std::mutex host_mutex;
std::condition_variable host_condition;
std::vector<std::unique_ptr<Task>> tasks;
std::vector<std::unique_ptr<Peer>> peers;
std::vector<rodakos_test::display_host::SentFrame> sent_frames;
thread_local Task* current_task = nullptr;
thread_local char external_task;
bool block_next_send = false;
bool send_blocked = false;
bool release_send = false;
bool close_overlap = false;
bool cleanup_entered = false;
std::atomic<int> send_result{ESP_PEER_ERR_NONE};
std::function<void()> next_send_return;
std::atomic<int64_t> clock_offset_us{0};
size_t main_loop_count = 0;
size_t clock_reads = 0;
std::atomic<int64_t> main_loop_cost_us{0};
std::atomic<int64_t> send_cost_us{0};
int open_result = ESP_PEER_ERR_NONE;
bool default_impl_available = true;
int connection_result = ESP_PEER_ERR_NONE;
bool task_creation_allowed = true;
}

namespace rodakos_test::display_host {
void JoinTasks() {
    for (const auto& task : tasks) if (task->thread.joinable()) task->thread.join();
    tasks.clear();
}
void Reset() {
    JoinTasks();
    std::lock_guard<std::mutex> lock(host_mutex);
    peers.clear();
    sent_frames.clear();
    block_next_send = send_blocked = release_send = close_overlap = false;
    cleanup_entered = false;
    send_result = open_result = ESP_PEER_ERR_NONE;
    next_send_return = {};
    clock_offset_us = 0;
    main_loop_count = 0;
    clock_reads = 0;
    main_loop_cost_us = send_cost_us = 0;
    connection_result = ESP_PEER_ERR_NONE;
    default_impl_available = task_creation_allowed = true;
}
esp_peer_handle_t LatestPeer() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return peers.empty() ? nullptr : peers.back().get();
}
void OpenControlChannel(esp_peer_handle_t peer, uint16_t stream_id) {
    auto* value = static_cast<Peer*>(peer);
    esp_peer_data_channel_info_t channel{};
    channel.label = const_cast<char*>("screen-control");
    channel.stream_id = stream_id;
    value->config.on_channel_open(&channel, value->config.ctx);
}
void OpenVideoChannel(esp_peer_handle_t peer, uint16_t stream_id) {
    auto* value = static_cast<Peer*>(peer);
    esp_peer_data_channel_info_t channel{};
    channel.label = const_cast<char*>("screen-jpeg");
    channel.stream_id = stream_id;
    value->config.on_channel_open(&channel, value->config.ctx);
}
void Receive(esp_peer_handle_t peer, uint16_t stream_id, const std::string& payload) {
    auto* value = static_cast<Peer*>(peer);
    esp_peer_data_frame_t frame{};
    frame.type = ESP_PEER_DATA_CHANNEL_DATA;
    frame.stream_id = stream_id;
    frame.data = reinterpret_cast<uint8_t*>(const_cast<char*>(payload.data()));
    frame.size = static_cast<int>(payload.size());
    value->config.on_data(&frame, value->config.ctx);
}
void EmitState(esp_peer_handle_t peer, esp_peer_state_t state) {
    auto* value = static_cast<Peer*>(peer);
    value->config.on_state(state, value->config.ctx);
}
void CloseControlChannel(esp_peer_handle_t peer, uint16_t stream_id) {
    auto* value = static_cast<Peer*>(peer);
    esp_peer_data_channel_info_t channel{};
    channel.label = const_cast<char*>("screen-control");
    channel.stream_id = stream_id;
    value->config.on_channel_close(&channel, value->config.ctx);
}
std::vector<SentFrame> SentFrames() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return sent_frames;
}
bool WaitForSendReturns(size_t minimum) {
    std::unique_lock<std::mutex> lock(host_mutex);
    return host_condition.wait_for(lock, std::chrono::seconds(2), [&] {
        return static_cast<size_t>(std::count_if(sent_frames.begin(), sent_frames.end(),
            [](const auto& frame) { return frame.returned; })) >= minimum;
    });
}
bool WaitForVideoSendReturns(uint16_t stream_id, size_t minimum) {
    std::unique_lock<std::mutex> lock(host_mutex);
    return host_condition.wait_for(lock, std::chrono::seconds(2), [&] {
        return static_cast<size_t>(std::count_if(sent_frames.begin(), sent_frames.end(),
            [&](const auto& frame) { return frame.returned && frame.stream_id == stream_id; })) >= minimum;
    });
}
size_t MainLoopCount() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return main_loop_count;
}
bool IsClosed(esp_peer_handle_t peer) {
    std::lock_guard<std::mutex> lock(host_mutex);
    return static_cast<Peer*>(peer)->closed;
}
bool CloseOverlappedSend() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return close_overlap;
}
void BlockNextSend() {
    std::lock_guard<std::mutex> lock(host_mutex);
    block_next_send = true;
    send_blocked = release_send = false;
    cleanup_entered = false;
}
bool WaitForBlockedSend() {
    std::unique_lock<std::mutex> lock(host_mutex);
    return host_condition.wait_for(lock, std::chrono::seconds(2), [] { return send_blocked; });
}
void ReleaseSend() {
    std::lock_guard<std::mutex> lock(host_mutex);
    release_send = true;
    host_condition.notify_all();
}
void NotifyCleanupEntry() {
    std::lock_guard<std::mutex> lock(host_mutex);
    cleanup_entered = true;
    host_condition.notify_all();
}
bool WaitForCleanupEntry() {
    std::unique_lock<std::mutex> lock(host_mutex);
    return host_condition.wait_for(lock, std::chrono::seconds(2), [] { return cleanup_entered; });
}
void SetSendResult(int result) { send_result = result; }
void OnNextSendReturn(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(host_mutex);
    next_send_return = std::move(callback);
}
void SetOpenResult(int result) { open_result = result; }
void SetDefaultImplAvailable(bool available) { default_impl_available = available; }
void SetConnectionResult(int result) { connection_result = result; }
void SetTaskCreationAllowed(bool allowed) { task_creation_allowed = allowed; }
void AdvanceTimeUs(int64_t delta) { clock_offset_us.fetch_add(delta); }
void RunPeerTasks() {
    std::lock_guard<std::mutex> lock(host_mutex);
    for (const auto& task : tasks) task->ready = true;
    host_condition.notify_all();
}
bool WaitForMainLoops(size_t minimum) {
    std::unique_lock<std::mutex> lock(host_mutex);
    return host_condition.wait_for(lock, std::chrono::seconds(2),
        [&] { return main_loop_count >= minimum; });
}
size_t ClockReads() {
    std::lock_guard<std::mutex> lock(host_mutex);
    return clock_reads;
}
bool WaitForClockReads(size_t minimum) {
    std::unique_lock<std::mutex> lock(host_mutex);
    return host_condition.wait_for(lock, std::chrono::seconds(2), [&] { return clock_reads >= minimum; });
}
void SetMainLoopCostUs(int64_t value) { main_loop_cost_us = value; }
void SetSendCostUs(int64_t value) { send_cost_us = value; }
}

BaseType_t xTaskCreateWithCaps(void (*entry)(void*), const char*, size_t, void* arg,
                              unsigned, TaskHandle_t* output, unsigned) {
    if (!task_creation_allowed) return 0;
    auto task = std::make_unique<Task>();
    auto* raw = task.get();
    {
        std::lock_guard<std::mutex> lock(host_mutex);
        tasks.push_back(std::move(task));
    }
    raw->thread = std::thread([raw, entry, arg] {
        current_task = raw;
        {
            std::unique_lock<std::mutex> lock(host_mutex);
            host_condition.wait(lock, [raw] { return raw->ready; });
        }
        entry(arg);
        current_task = nullptr;
    });
    *output = raw;
    return pdPASS;
}
TaskHandle_t xTaskGetCurrentTaskHandle() {
    return current_task ? static_cast<void*>(current_task) : &external_task;
}
void vTaskDelay(TickType_t) {
    // Stop waits here after closing the tracker. Let the real peer task observe
    // that stop request and execute its production FinishStop path.
    {
        std::lock_guard<std::mutex> lock(host_mutex);
        for (const auto& task : tasks) task->ready = true;
        host_condition.notify_all();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
void vTaskDeleteWithCaps(TaskHandle_t) {}
int64_t esp_timer_get_time() {
    const int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() + clock_offset_us.load();
    std::lock_guard<std::mutex> lock(host_mutex);
    ++clock_reads;
    host_condition.notify_all();
    return now;
}

extern "C" {
const esp_peer_ops_t* esp_peer_get_default_impl() {
    static const esp_peer_ops_t operations{};
    return default_impl_available ? &operations : nullptr;
}
int esp_peer_open(esp_peer_cfg_t* config, const esp_peer_ops_t*, esp_peer_handle_t* output) {
    std::lock_guard<std::mutex> lock(host_mutex);
    if (open_result != ESP_PEER_ERR_NONE) return open_result;
    auto peer = std::make_unique<Peer>();
    peer->config = *config;
    *output = peer.get();
    peers.push_back(std::move(peer));
    return ESP_PEER_ERR_NONE;
}
int esp_peer_new_connection(esp_peer_handle_t) { return connection_result; }
int esp_peer_main_loop(esp_peer_handle_t) {
    std::lock_guard<std::mutex> lock(host_mutex);
    clock_offset_us.fetch_add(main_loop_cost_us.load());
    ++main_loop_count;
    host_condition.notify_all();
    return ESP_PEER_ERR_NONE;
}
int esp_peer_create_data_channel(esp_peer_handle_t, esp_peer_data_channel_cfg_t*) {
    return ESP_PEER_ERR_NONE;
}
int esp_peer_send_msg(esp_peer_handle_t, esp_peer_msg_t*) { return ESP_PEER_ERR_NONE; }
int esp_peer_send_data(esp_peer_handle_t handle, esp_peer_data_frame_t* frame) {
    std::unique_lock<std::mutex> lock(host_mutex);
    auto* peer = static_cast<Peer*>(handle);
    if (peer->closed) return ESP_PEER_ERR_WRONG_STATE;
    peer->sending = true;
    // Capture the bytes and borrowed peer at the actual production API call.
    const size_t attempt = sent_frames.size();
    sent_frames.push_back({handle, frame->stream_id, frame->type,
        std::string(reinterpret_cast<const char*>(frame->data), frame->size)});
    if (block_next_send) {
        block_next_send = false;
        send_blocked = true;
        host_condition.notify_all();
        host_condition.wait(lock, [] { return release_send; });
    }
    peer->sending = false;
    clock_offset_us.fetch_add(send_cost_us.load());
    const int result = send_result.load();
    // Test callbacks only revoke a captured lease atomically. The SDK call has
    // already been admitted; revocation must affect its next fragment/retry.
    auto callback = std::move(next_send_return);
    if (callback) callback();
    sent_frames[attempt].result = result;
    sent_frames[attempt].returned = true;
    host_condition.notify_all();
    return result;
}
int esp_peer_close(esp_peer_handle_t handle) {
    std::lock_guard<std::mutex> lock(host_mutex);
    auto* peer = static_cast<Peer*>(handle);
    close_overlap = close_overlap || peer->sending;
    peer->closed = true;
    return ESP_PEER_ERR_NONE;
}
}
