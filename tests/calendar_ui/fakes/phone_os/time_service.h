#pragma once
// Host fake for the Calendar UI tests: a settable wall clock and SNTP status.
#include <cstdint>
#include <ctime>
#include <string>

enum class TimeSyncStatus {
    kReset,
    kInProgress,
    kCompleted,
};

namespace calendar_test {
inline bool time_valid = true;
inline std::time_t now = 0;
inline TimeSyncStatus sync_status = TimeSyncStatus::kReset;
inline bool sync_starts = true;
inline int sync_calls = 0;
}  // namespace calendar_test

inline void TimeServiceApplySavedTimeZone() {}
inline bool TimeServiceTimeIsValid() { return calendar_test::time_valid; }
inline bool TimeServiceStartSavedSync() {
    ++calendar_test::sync_calls;
    if (!calendar_test::sync_starts) return false;
    calendar_test::sync_status = TimeSyncStatus::kInProgress;
    return true;
}
inline TimeSyncStatus TimeServiceGetSyncStatus() { return calendar_test::sync_status; }
inline bool TimeServiceUnixTimeMs(int64_t& unix_ms) {
    unix_ms = static_cast<int64_t>(calendar_test::now) * 1000;
    return calendar_test::time_valid;
}
