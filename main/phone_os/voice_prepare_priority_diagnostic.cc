#include "phone_os/voice_prepare_priority_diagnostic.h"

#ifdef RODAKOS_RELEASE_TESTS
#include "phone_os/voice_prepare_priority_observer.h"

#include <cinttypes>
#include <cstdio>

namespace rodakos {
namespace {

const char* StageName(VoicePreparePriorityStage stage) {
    switch (stage) {
        case VoicePreparePriorityStage::kPrepareBegin: return "prepare_begin";
        case VoicePreparePriorityStage::kOpenAcquired: return "open_acquired";
        case VoicePreparePriorityStage::kCloudReturned: return "cloud_returned";
        case VoicePreparePriorityStage::kOpenReleased: return "open_released";
        case VoicePreparePriorityStage::kExitNoOpen: return "exit_no_open";
    }
    return "invalid";
}

}  // namespace

void PrintVoicePreparePrioritySnapshot() {
    VoicePreparePrioritySnapshot snapshot;
    if (!TryTakeCompletedVoicePreparePrioritySnapshot(snapshot)) {
        std::fprintf(stdout,
                     "RODAK_VOICE_PREPARE_TRACE {\"phase\":\"unavailable\",\"ok\":false}\n");
        std::fflush(stdout);
        return;
    }
    if (snapshot.count > kVoicePreparePrioritySampleLimit) {
        std::fprintf(stdout,
                     "RODAK_VOICE_PREPARE_TRACE {\"phase\":\"invalid_snapshot\",\"ok\":false}\n");
        std::fflush(stdout);
        return;
    }

    // Hex keeps arbitrary task-name bytes out of the JSON syntax without allocating.
    constexpr char kHex[] = "0123456789abcdef";
    char name_hex[kVoicePreparePriorityTaskNameBytes * 2 + 1]{};
    for (uint32_t i = 0; i < kVoicePreparePriorityTaskNameBytes && snapshot.task_name[i] != '\0'; ++i) {
        const auto value = static_cast<unsigned char>(snapshot.task_name[i]);
        name_hex[i * 2] = kHex[value >> 4];
        name_hex[i * 2 + 1] = kHex[value & 15];
    }
    // Each JSON line is one stdio call. Other console logs may appear between lines;
    // scope/index plus the final count are required before accepting a complete record.
    std::fprintf(stdout,
                 "RODAK_VOICE_PREPARE_TRACE {\"phase\":\"snapshot\",\"ok\":true,"
                 "\"scope_id\":%" PRIu32 ",\"self_handle\":%" PRIuPTR ","
                 "\"task_name_hex\":\"%s\",\"count\":%" PRIu32 ",\"flags\":%" PRIu32 ","
                 "\"rejected_begin_count\":%" PRIu32 ",\"open_acquired\":%s}\n",
                 snapshot.scope_id, snapshot.self_handle, name_hex, snapshot.count,
                 snapshot.flags, snapshot.rejected_begin_count,
                 snapshot.open_acquired ? "true" : "false");
    for (uint32_t i = 0; i < snapshot.count; ++i) {
        const auto& sample = snapshot.samples[i];
        std::fprintf(stdout,
                     "RODAK_VOICE_PREPARE_TRACE {\"phase\":\"sample\",\"scope_id\":%" PRIu32 ","
                     "\"index\":%" PRIu32 ",\"stage\":\"%s\",\"self_handle\":%" PRIuPTR ","
                     "\"effective_priority\":%" PRIu32 ",\"before_us\":%" PRId64 ","
                     "\"after_us\":%" PRId64 "}\n",
                     snapshot.scope_id, i, StageName(sample.stage), sample.self_handle,
                     sample.effective_priority, sample.before_us, sample.after_us);
    }
    std::fprintf(stdout,
                 "RODAK_VOICE_PREPARE_TRACE {\"phase\":\"complete\",\"ok\":true,"
                 "\"scope_id\":%" PRIu32 ",\"count\":%" PRIu32 "}\n",
                 snapshot.scope_id, snapshot.count);
    std::fflush(stdout);
}

}  // namespace rodakos
#endif
