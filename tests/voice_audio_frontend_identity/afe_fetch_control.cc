#include "afe_fetch_control.h"

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
void OnFeed(const int16_t* samples) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!scripted) return;
    ++operations;
    ++feed_calls;
    for (size_t index = 0; index < 320; ++index) fed_samples.push_back(samples[index * 2]);
    if (feed_calls == block_feed && fetch_calls < unblock_at_fetch) {
        blocked_feed = feed_calls;
        changed.notify_all();
        changed.wait(lock, [&] { return released || fetch_calls >= unblock_at_fetch; });
    }
    --operations;
    changed.notify_all();
}
afe_fetch_result_t* OnFetch(TickType_t) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!scripted) return nullptr;
    ++operations;
    ++fetch_calls;
    changed.notify_all();
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
    std::lock_guard<std::mutex> lock(mutex);
    if (level == 'W' && std::string(message).rfind("AFE fetch rejected:", 0) == 0)
        ++rejected_warnings;
    unsigned generation, failures, cancelled;
    if (std::sscanf(message,
                    "AFE fetch stopped: generation=%u current_failures=%u cancelled_results=%u",
                    &generation, &failures, &cancelled) == 3) {
        summary = {true, generation, failures, cancelled};
    }
    changed.notify_all();
}
}  // namespace rodakos_test::afe_fetch
