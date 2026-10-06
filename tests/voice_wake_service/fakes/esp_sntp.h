#pragma once
#include <sys/time.h>
using sntp_sync_status_t = int;
constexpr int SNTP_SYNC_STATUS_RESET = 0;
constexpr int SNTP_SYNC_STATUS_IN_PROGRESS = 1;
constexpr int SNTP_SYNC_STATUS_COMPLETED = 2;
constexpr int SNTP_OPMODE_POLL = 0;
inline void esp_sntp_setservername(int, const char*) {}
inline void esp_sntp_set_time_sync_notification_cb(void (*)(timeval*)) {}
inline bool esp_sntp_enabled() { return false; }
inline void esp_sntp_stop() {}
inline void esp_sntp_set_sync_status(int) {}
inline void esp_sntp_setoperatingmode(int) {}
inline void esp_sntp_init() {}
inline sntp_sync_status_t esp_sntp_get_sync_status() { return SNTP_SYNC_STATUS_RESET; }
