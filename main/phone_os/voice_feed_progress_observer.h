#pragma once

#include <cstdint>

namespace rodakos {

inline constexpr uint32_t kVoiceFeedProgressOk = 0;
inline constexpr uint32_t kVoiceFeedProgressBusy = 1;
inline constexpr uint32_t kVoiceFeedProgressUnavailable = 2;
inline constexpr uint32_t kVoiceFeedProgressStale = 3;
inline constexpr uint32_t kVoiceFeedProgressExpired = 4;
inline constexpr uint32_t kVoiceFeedProgressConflict = 5;
inline constexpr uint32_t kVoiceFeedProgressExhausted = 6;
inline constexpr uint32_t kVoiceFeedProgressOpen = 7;
inline constexpr uint32_t kVoiceFeedProgressRetired = 8;

inline constexpr uint32_t kVoiceFeedProgressArmAfterUnknown = 1U << 0;
inline constexpr uint32_t kVoiceFeedProgressCloseOverrun = 1U << 1;
inline constexpr uint32_t kVoiceFeedProgressClockInvalid = 1U << 2;
inline constexpr uint32_t kVoiceFeedProgressDropped = 1U << 3;
inline constexpr uint32_t kVoiceFeedProgressSaturated = 1U << 4;
inline constexpr uint32_t kVoiceFeedProgressUnfinishedReplaced = 1U << 5;
inline constexpr uint32_t kVoiceFeedProgressDeadlineReached = 1U << 6;
inline constexpr uint32_t kVoiceFeedProgressProducerUnaligned = 1U << 7;
inline constexpr uint32_t kVoiceFeedProgressSamplingRetired = 1U << 8;

struct VoiceFeedProgressTicket {
    uint32_t generation = 0;
    uint32_t epoch = 0;
    uint32_t sequence = 0;
    uint32_t ticket = 0;
    uint32_t status = kVoiceFeedProgressUnavailable;
    uintptr_t target_handle = 0;
    int64_t api_begin_us = 0;
    int64_t arm_before_us = 0;
    int64_t arm_after_us = 0;
};

// Only scalar values cross this boundary; no TCB, stack or mutex is exposed.
struct VoiceFeedProgressSnapshot {
    VoiceFeedProgressTicket identity;
    uint32_t status = kVoiceFeedProgressUnavailable;
    uint32_t flags = 0;
    uint32_t target_samples = 0;
    uint32_t other_samples = 0;
    uint32_t baseline_drops = 0;
    uint32_t published_drops = 0;
    uintptr_t last_other_handle = 0;
    int64_t deadline_us = 0;
    int64_t first_target_begin_us = 0;
    int64_t last_target_end_us = 0;
    int64_t first_other_begin_us = 0;
    int64_t last_other_end_us = 0;
    int64_t api_return_us = 0;
    int64_t freeze_before_us = 0;
    int64_t freeze_after_us = 0;
    bool strict_counts_known = false;
};

VoiceFeedProgressTicket ArmVoiceFeedProgress(uint32_t generation, uint32_t epoch,
                                           uint32_t sequence, uintptr_t target_handle,
                                           int64_t api_begin_us, int64_t afe_started_us);
void CloseVoiceFeedProgress(const VoiceFeedProgressTicket& ticket, int64_t api_return_us,
                            VoiceFeedProgressSnapshot& snapshot);
void SnapshotOpenVoiceFeedProgress(uint32_t generation, uint32_t epoch, uint32_t sequence,
                                   VoiceFeedProgressSnapshot& snapshot);
void LogVoiceFeedProgress(const char* event, const VoiceFeedProgressSnapshot& snapshot,
                          int64_t credit_published_us = 0);
void LogVoiceFeedProgressSummary(uint32_t generation);
void RetireVoiceFeedProgress(uint32_t generation);

// Core 0's existing tick hook supplies the current opaque identity and getter envelope.
void ObserveVoiceFeedProgressTick(int64_t getter_begin_us, uintptr_t current_handle,
                                  int64_t getter_end_us);

}  // namespace rodakos
