#pragma once

#ifdef RODAKOS_RELEASE_TESTS
#include <cstdint>

namespace rodakos {

inline constexpr uint32_t kVoicePreparePrioritySampleLimit = 4;
inline constexpr uint32_t kVoicePreparePriorityTaskNameBytes = 16;
inline constexpr uint32_t kVoicePreparePriorityClockInvalid = 1U << 0;
inline constexpr uint32_t kVoicePreparePriorityInvalidOrder = 1U << 1;
inline constexpr uint32_t kVoicePreparePriorityOverflow = 1U << 2;
inline constexpr uint32_t kVoicePreparePriorityNameTruncated = 1U << 3;
inline constexpr uint32_t kVoicePreparePriorityIdentityInvalid = 1U << 4;

enum class VoicePreparePriorityStage : uint32_t {
    kPrepareBegin,
    kOpenAcquired,
    kCloudReturned,
    kOpenReleased,
    kExitNoOpen,
};

enum class VoicePreparePriorityStatus : uint32_t {
    kOk,
    kBusy,
    kUnavailable,
    kStale,
    kWrongOwner,
    kInvalidStage,
    kExhausted,
};

struct VoicePreparePriorityTicket {
    uint32_t scope_id = 0;
    uintptr_t self_handle = 0;
    VoicePreparePriorityStatus status = VoicePreparePriorityStatus::kUnavailable;
};

struct VoicePreparePrioritySample {
    int64_t before_us = 0;
    int64_t after_us = 0;
    uintptr_t self_handle = 0;
    uint32_t effective_priority = 0;
    VoicePreparePriorityStage stage = VoicePreparePriorityStage::kPrepareBegin;
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(VoicePreparePrioritySample) == 32);
#endif

struct VoicePreparePrioritySnapshot {
    uint32_t scope_id = 0;
    uintptr_t self_handle = 0;
    char task_name[kVoicePreparePriorityTaskNameBytes]{};
    uint32_t count = 0;
    VoicePreparePrioritySample samples[kVoicePreparePrioritySampleLimit]{};
    uint32_t flags = 0;
    // Cumulative, saturating rejected Begin count observed when this snapshot is taken.
    uint32_t rejected_begin_count = 0;
    bool open_acquired = false;
};

VoicePreparePriorityTicket BeginVoicePreparePriority();
VoicePreparePriorityStatus MarkVoicePreparePriority(const VoicePreparePriorityTicket& ticket,
                                                   VoicePreparePriorityStage stage);
VoicePreparePriorityStatus EndVoicePreparePriority(const VoicePreparePriorityTicket& ticket);

// Active records cannot be taken. Success consumes ready diagnostic state, but never
// queries another task, changes business state, or resets the scope ID high watermark.
bool TryTakeCompletedVoicePreparePrioritySnapshot(VoicePreparePrioritySnapshot& out);

// Declare before the business open_lock so its destructor observes after lock release.
class VoicePreparePriorityScope {
public:
    VoicePreparePriorityScope();
    ~VoicePreparePriorityScope();
    VoicePreparePriorityStatus Mark(VoicePreparePriorityStage stage);
    VoicePreparePriorityScope(const VoicePreparePriorityScope&) = delete;
    VoicePreparePriorityScope& operator=(const VoicePreparePriorityScope&) = delete;
private:
    VoicePreparePriorityTicket ticket_;
};

#ifdef RODAKOS_PREPARE_PRIORITY_OBSERVER_HOST_TEST
// Host-only saturation/isolation seam; never present in TEST device firmware.
void ResetVoicePreparePriorityObserverForHostTests(uint32_t last_scope_id = 0,
                                                 uint32_t rejected_begin_count = 0);
#endif

}  // namespace rodakos
#endif
