#pragma once

#include <cstdint>

namespace rodakos {

// Only the TEST translation unit owns storage. No frontend layout depends on this header.
struct VoiceTickCoreSnapshot {
    int64_t window_open_us;
    int64_t deadline_us;
    int64_t last_published_sample_end_us;
    int64_t max_previous_call_us;
    int64_t max_current_call_us;
    int64_t last_suspended_sample_end_us;
    int64_t last_cache_disabled_sample_end_us;
    int64_t first_published_sample_end_us;
    uint32_t window_token;
    uint32_t flags;
    uint32_t published_calls;
    uint32_t published_drops;
    uint32_t baseline_calls;
    uint32_t baseline_drops;
    uint32_t max_sample_span_us;
    uint32_t max_state_bits;
    uint32_t suspended_samples;
    uint32_t cache_disabled_samples;
};

struct VoiceTickSnapshot {
    VoiceTickCoreSnapshot cores[2];
    int64_t afe_event_observed_us;
    int64_t freeze_begin_us;
    int64_t freeze_end_us;
    uint32_t generation;
    uint32_t epoch;
    uint32_t window_token;
    uint32_t gap_id;
    uint32_t status;
    uint32_t registered_mask;
};

inline constexpr uint32_t kVoiceTickOk = 0;
inline constexpr uint32_t kVoiceTickBusy = 1;
inline constexpr uint32_t kVoiceTickUnavailable = 2;
inline constexpr uint32_t kVoiceTickStale = 3;
inline constexpr uint32_t kVoiceTickExpired = 4;
inline constexpr uint32_t kVoiceTickTokenExhausted = 5;
inline constexpr uint32_t kVoiceTickPrefixUncovered = 1U << 0;
inline constexpr uint32_t kVoiceTickCrossingInterval = 1U << 1;
inline constexpr uint32_t kVoiceTickCrossingSample = 1U << 2;
inline constexpr uint32_t kVoiceTickDropped = 1U << 3;
inline constexpr uint32_t kVoiceTickClockInvalid = 1U << 4;
inline constexpr uint32_t kVoiceTickCounterSaturated = 1U << 5;
inline constexpr uint32_t kVoiceTickDeadlineReached = 1U << 6;
inline constexpr uint32_t kVoiceTickBeforeWindow = 1U << 7;

// Initialize once from the serialized AFE lifecycle, before its first Fetch task exists.
bool InitializeVoiceTickObserver();
struct VoiceTickBeginSnapshot { int64_t before_us; int64_t after_us; uint32_t token; };
uint32_t BeginVoiceTickObservation(uint32_t generation, uint32_t epoch, int64_t began_us,
                                   VoiceTickBeginSnapshot* begin = nullptr);
void LogVoiceTickBegin(uint32_t generation, uint32_t epoch, const VoiceTickBeginSnapshot& begin);
uint32_t CurrentVoiceTickObservation(uint32_t generation, uint32_t epoch);
uint32_t FreezeVoiceTickObservation(uint32_t generation, uint32_t epoch,
                                  uint32_t expected_token, uint32_t gap_id,
                                  int64_t afe_event_observed_us, bool finish,
                                  VoiceTickSnapshot& snapshot);
void LogVoiceTickObservation(const char* event, const VoiceTickSnapshot& snapshot);
uint32_t ObserveVoiceTickBoundary(const char* event, uint32_t generation, uint32_t epoch,
                                 uint32_t expected_token, uint32_t gap_id,
                                 int64_t afe_event_observed_us, bool finish = false);

}  // namespace rodakos
