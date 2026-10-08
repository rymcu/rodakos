#include "afe_fetch_control.h"
#include "observation_control.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace rodakos_test::afe_fetch {
namespace {
using namespace std::chrono_literals;
struct Step {
    FetchReply reply;
    afe_fetch_result_t result;
    bool entered = false;
    bool released = false;
};
std::mutex mutex;
std::condition_variable changed;
std::deque<std::unique_ptr<Step>> steps;
size_t next_step = 0;
bool scripted = false;
bool released = false;
size_t fetch_calls = 0;
size_t feed_calls = 0;
size_t block_feed = 0;
size_t unblock_at_fetch = 0;
size_t blocked_feed = 0;
size_t operations = 0;
size_t destroy_during_operation = 0;
size_t rejected_warnings = 0;
FetchSummary summary;
std::vector<int16_t> fed_samples;
bool streaming = false;
bool first_feed_blocked = false;
bool first_feed_released = false;
int reset_result = 1;
std::deque<int> feed_results;
std::deque<size_t> streaming_failures;
std::deque<int16_t> web_samples;
std::deque<int16_t> ring_samples;
std::vector<int16_t> stream_output;
afe_fetch_result_t stream_result;
FlowStats flow;
#if CONFIG_USE_DEVICE_AEC
constexpr int default_feed_samples = 256;
#else
constexpr int default_feed_samples = 160;
#endif
int format_feed = default_feed_samples;
int format_fetch = 512;
int format_channels = 1;
int format_rate = 16000;
}

void BeginScript() {
    std::lock_guard<std::mutex> lock(mutex);
    steps.clear();
    next_step = 0;
    scripted = true;
    released = false;
    fetch_calls = feed_calls = block_feed = unblock_at_fetch = blocked_feed = 0;
    operations = destroy_during_operation = rejected_warnings = 0;
    summary = {};
    fed_samples.clear();
    streaming = first_feed_blocked = first_feed_released = false;
    reset_result = 1;
    feed_results.clear();
    streaming_failures.clear();
    web_samples.clear();
    ring_samples.clear();
    flow = {};
    format_feed = default_feed_samples;
    format_fetch = 512;
    format_channels = 1;
    format_rate = 16000;
}
void ResetToDefault() {
    BeginScript();
    std::lock_guard<std::mutex> lock(mutex);
    scripted = false;
}
size_t QueueFetch(FetchReply reply) {
    std::lock_guard<std::mutex> lock(mutex);
    auto step = std::make_unique<Step>();
    step->reply = std::move(reply);
    step->result.ret_value = step->reply.status;
    step->result.data = step->reply.samples.empty() ? nullptr : step->reply.samples.data();
    step->result.data_size = static_cast<int>(step->reply.samples.size() * sizeof(int16_t));
    step->result.vad_state = step->reply.vad_state;
    steps.push_back(std::move(step));
    changed.notify_all();
    return steps.size();
}
bool WaitFetchEntered(size_t ticket) {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 3s, [&] {
        return ticket > 0 && ticket <= steps.size() && steps[ticket - 1]->entered;
    });
}
void ReleaseFetch(size_t ticket) {
    std::lock_guard<std::mutex> lock(mutex);
    if (ticket > 0 && ticket <= steps.size()) steps[ticket - 1]->released = true;
    changed.notify_all();
}
void BlockFeedUntilFetch(size_t feed_call, size_t fetch_call) {
    std::lock_guard<std::mutex> lock(mutex);
    block_feed = feed_call;
    unblock_at_fetch = fetch_call;
}
bool WaitFeedBlocked(size_t feed_call) {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 3s, [&] { return blocked_feed == feed_call; });
}
void ForceReleaseAll() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
}
void BeginStreaming(bool block_first) {
    BeginScript();
    std::lock_guard<std::mutex> lock(mutex);
    streaming = true;
    first_feed_blocked = block_first;
}
void SetFeedResult(int result) {
    std::lock_guard<std::mutex> lock(mutex);
    feed_results.push_back(result);
}
void SetResetResult(int result) {
    std::lock_guard<std::mutex> lock(mutex);
    reset_result = result;
}
void QueueStreamingFailure(size_t count) {
    std::lock_guard<std::mutex> lock(mutex);
    streaming_failures.push_back(count);
}
void ReleaseFirstFeed() {
    std::lock_guard<std::mutex> lock(mutex);
    first_feed_released = true;
    changed.notify_all();
}
bool WaitFeedReturns(size_t count) {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 3s, [&] { return flow.returns >= count; });
}
bool WaitFlowResets(size_t count) {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 3s, [&] { return flow.resets >= count; });
}
bool WaitFlowSamples(size_t count) {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 3s, [&] { return flow.valid_samples >= count; });
}
bool WaitFlowStalls(size_t count) {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 3s, [&] { return flow.stalls >= count; });
}
FlowStats FlowSnapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    auto result = flow;
    result.queued_samples = ring_samples.size();
    return result;
}
int FeedChunkSamples() { std::lock_guard<std::mutex> lock(mutex); return format_feed; }
int FetchChunkSamples() { std::lock_guard<std::mutex> lock(mutex); return format_fetch; }
int FetchChannels() { std::lock_guard<std::mutex> lock(mutex); return format_channels; }
int SampleRate() { std::lock_guard<std::mutex> lock(mutex); return format_rate; }
void SetFormat(int feed_samples, int fetch_samples, int fetch_channels, int sample_rate) {
    std::lock_guard<std::mutex> lock(mutex);
    format_feed = feed_samples;
    format_fetch = fetch_samples;
    format_channels = fetch_channels;
    format_rate = sample_rate;
}
int ResetBuffer() {
    std::lock_guard<std::mutex> lock(mutex);
    ++flow.resets;
    if (operations != 0) ++flow.reset_during_operation;
    if (reset_result == 1) ring_samples.clear();
    changed.notify_all();
    return reset_result;
}
int ResetVad() {
    std::lock_guard<std::mutex> lock(mutex);
    ++flow.vad_resets;
    if (operations != 0) ++flow.reset_during_operation;
    changed.notify_all();
    return reset_result;
}
size_t RejectedWarningCount() {
    std::lock_guard<std::mutex> lock(mutex);
    return rejected_warnings;
}
FetchSummary LastSummary() {
    std::lock_guard<std::mutex> lock(mutex);
    return summary;
}
size_t DestroyDuringOperationCount() {
    std::lock_guard<std::mutex> lock(mutex);
    return destroy_during_operation;
}
std::vector<int16_t> FedMicrophoneSamples() {
    std::lock_guard<std::mutex> lock(mutex);
    return fed_samples;
}
int OnFeed(const int16_t* samples) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!scripted) return 320;
    ++operations;
    ++feed_calls;
    ++flow.feeds;
    const size_t count = static_cast<size_t>(format_feed);
    for (size_t index = 0; index < count; ++index) fed_samples.push_back(samples[index * 2]);
    if (streaming && first_feed_blocked && feed_calls == 1) {
        blocked_feed = feed_calls;
        changed.notify_all();
        changed.wait(lock, [&] { return released || first_feed_released; });
    }
    if (feed_calls == block_feed && fetch_calls < unblock_at_fetch) {
        blocked_feed = feed_calls;
        changed.notify_all();
        changed.wait(lock, [&] { return released || fetch_calls >= unblock_at_fetch; });
    }
    static constexpr int output_pattern[] = {320, 640, 320, 640, 640};
    int written = count == 256 ? output_pattern[(feed_calls - 1) % 5] : 320;
    if (streaming) {
        for (size_t index = 0; index < count; ++index) web_samples.push_back(samples[index * 2]);
        const size_t produced = web_samples.size() / 160 * 160;
        written = static_cast<int>(produced * sizeof(int16_t));
        if (!feed_results.empty()) { written = feed_results.front(); feed_results.pop_front(); }
        for (size_t index = 0; index < produced; ++index) {
            // Negative returns may have written a partial block before failing.
            if (written < 0 || index < static_cast<size_t>(written) / 2) ring_samples.push_back(web_samples.front());
            web_samples.pop_front();
        }
    } else if (!feed_results.empty()) {
        written = feed_results.front();
        feed_results.pop_front();
    }
    --operations;
    ++flow.returns;
    changed.notify_all();
    return written;
}
afe_fetch_result_t* OnFetch(TickType_t timeout) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!scripted) return nullptr;
    ++operations;
    ++fetch_calls;
    ++flow.fetches;
    changed.notify_all();
    if (streaming) {
        stream_output.clear();
        if (!streaming_failures.empty()) {
            const size_t consumed = std::min(streaming_failures.front(), ring_samples.size());
            streaming_failures.pop_front();
            for (size_t index = 0; index < consumed; ++index) ring_samples.pop_front();
            flow.partial_lost += consumed;
            stream_result = {};
        } else {
            while (stream_output.size() < 512) {
                while (!ring_samples.empty() && stream_output.size() < 512) {
                    stream_output.push_back(ring_samples.front());
                    ring_samples.pop_front();
                }
                if (stream_output.size() == 512) break;
                if (released || !changed.wait_for(lock, std::chrono::milliseconds(timeout), [&] {
                    return released || !ring_samples.empty();
                })) break;
            }
            stream_result = {};
            if (stream_output.size() == 512) {
                stream_result = {ESP_OK, stream_output.data(), 1024, VAD_SILENCE};
                flow.valid_samples += 512;
            } else {
                flow.partial_lost += stream_output.size();
            }
        }
        --operations;
        changed.notify_all();
        return &stream_result;
    }
    changed.wait(lock, [&] { return released || next_step < steps.size(); });
    if (next_step >= steps.size()) {
        --operations;
        return nullptr;
    }
    auto& step = *steps[next_step++];
    step.entered = true;
    changed.notify_all();
    changed.wait(lock, [&] { return released || step.released; });
    --operations;
    return &step.result;
}
void OnDestroy() {
    std::lock_guard<std::mutex> lock(mutex);
    if (operations != 0) ++destroy_during_operation;
}
void CaptureLog(char level, const char* tag, const char* format, ...) {
    if (std::string(tag) != "VoiceAudioFrontend") return;
    char message[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    std::unique_lock<std::mutex> lock(mutex);
    if (level == 'W' && std::string(message).rfind("AFE fetch rejected:", 0) == 0)
        ++rejected_warnings;
    if (level == 'W' && std::string(message).rfind("AFE input stalled:", 0) == 0)
        ++flow.stalls;
    if (level == 'W' && std::string(message).rfind("AFE feed rejected:", 0) == 0)
        ++flow.feed_errors;
    unsigned generation, failures, cancelled;
    if (std::sscanf(message,
                    "AFE fetch stopped: generation=%u current_failures=%u cancelled_results=%u",
                    &generation, &failures, &cancelled) == 3) {
        summary = {true, generation, failures, cancelled};
    }
    changed.notify_all();
    lock.unlock();
    rodakos_test::afe_observation::OnLog(message);
}
}  // namespace rodakos_test::afe_fetch
