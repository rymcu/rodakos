#include "phone_os/voice_prepare_priority_diagnostic.h"
#include "phone_os/voice_prepare_priority_observer.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace rodakos;
VoicePreparePrioritySnapshot next_snapshot;
bool ready = false;
unsigned take_calls = 0;

void PrintOnce() {
    const auto previous_calls = take_calls;
    PrintVoicePreparePrioritySnapshot();
    if (take_calls != previous_calls + 1) throw std::runtime_error("one take per output request");
}

void Complete(uint32_t scope, uint32_t count) {
    next_snapshot = {};
    next_snapshot.scope_id = scope;
    next_snapshot.self_handle = UINTPTR_MAX;
    std::strcpy(next_snapshot.task_name, "wake_notify");
    next_snapshot.count = count;
    next_snapshot.open_acquired = count != 2;
    next_snapshot.rejected_begin_count = UINT32_MAX;
    const VoicePreparePriorityStage stages[] = {
        VoicePreparePriorityStage::kPrepareBegin,
        count == 2 ? VoicePreparePriorityStage::kExitNoOpen : VoicePreparePriorityStage::kOpenAcquired,
        count == 3 ? VoicePreparePriorityStage::kOpenReleased : VoicePreparePriorityStage::kCloudReturned,
        VoicePreparePriorityStage::kOpenReleased,
    };
    for (uint32_t i = 0; i < count; ++i) {
        auto& sample = next_snapshot.samples[i];
        sample.stage = stages[i];
        sample.self_handle = next_snapshot.self_handle;
        sample.effective_priority = 4 + i;
        sample.before_us = INT64_C(9007199254740993) + 10 * i;
        sample.after_us = sample.before_us + 1;
    }
    ready = true;
}
}  // namespace

namespace rodakos {
// 仅替换 completed provider；实际格式化和 stdout 输出来自生产 TU。
bool TryTakeCompletedVoicePreparePrioritySnapshot(VoicePreparePrioritySnapshot& out) {
    ++take_calls;
    if (!ready) return false;
    out = next_snapshot;
    ready = false;
    return true;
}
}  // namespace rodakos

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("one scenario required");
        const std::string scenario = argv[1];
        if (scenario == "unavailable") {
            PrintOnce();
        } else if (scenario == "invalid") {
            Complete(10, 4);
            next_snapshot.count = kVoicePreparePrioritySampleLimit + 1;
            PrintOnce();
            PrintOnce();
        } else if (scenario == "full" || scenario == "cancel" || scenario == "no_open") {
            Complete(11, scenario == "no_open" ? 2 : scenario == "cancel" ? 3 : 4);
            PrintOnce();
        } else if (scenario == "name") {
            Complete(12, 4);
            const unsigned char bytes[16] = {'"', '\\', '\n', '\r', '\t', 1, 127, 128,
                                             255, '%', 's', '{', '}', '[', ']', '!'};
            std::memcpy(next_snapshot.task_name, bytes, sizeof(bytes));
            PrintOnce();
        } else if (scenario == "consume") {
            Complete(13, 4);
            PrintOnce();
            PrintOnce();
        } else if (scenario == "records") {
            Complete(101, 4);
            PrintOnce();
            Complete(202, 2);
            PrintOnce();
        } else {
            throw std::runtime_error("unknown scenario");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
