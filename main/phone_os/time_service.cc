#include "phone_os/time_service.h"

#include "settings.h"

#include <esp_log.h>
#include <esp_sntp.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace {
constexpr const char* TAG = "TimeService";
constexpr const char* kClockNamespace = "clock";
constexpr const char* kTimeZoneKey = "tz";
constexpr const char* kNtpServerKey = "ntp";
constexpr const char* kDefaultTimeZone = "CST-8";
constexpr const char* kDefaultNtpServer = "pool.ntp.org";

char g_active_sntp_server[64] = "pool.ntp.org";

const RodakTimeZone kTimeZones[] = {
    {"Shanghai (UTC+8)", "CST-8"},
    {"UTC", "UTC0"},
    {"Tokyo (UTC+9)", "JST-9"},
    {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
};

const RodakNtpServer kNtpServers[] = {
    {"Global pool", "pool.ntp.org"},
    {"NTP pool 0", "0.pool.ntp.org"},
    {"NTP pool 1", "1.pool.ntp.org"},
    {"Aliyun CN", "ntp.aliyun.com"},
    {"Tencent CN", "time1.cloud.tencent.com"},
    {"Apple", "time.apple.com"},
    {"Google", "time.google.com"},
    {"Custom", ""},
};

void TimeSyncNotificationCallback(struct timeval* tv) {
    if (tv == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "SNTP sync completed, epoch=%lld",
             static_cast<long long>(tv->tv_sec));
}

void ConfigureSntpServers(const std::string& preferred_server) {
    std::snprintf(g_active_sntp_server, sizeof(g_active_sntp_server), "%s",
                  preferred_server.c_str());
    esp_sntp_setservername(0, g_active_sntp_server);

#if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    size_t slot = 1;
    for (const auto& server : kNtpServers) {
        if (slot >= CONFIG_LWIP_SNTP_MAX_SERVERS) {
            break;
        }
        if (server.server[0] == '\0' || preferred_server == server.server) {
            continue;
        }
        esp_sntp_setservername(slot++, server.server);
    }
#endif

    esp_sntp_set_time_sync_notification_cb(TimeSyncNotificationCallback);
}

}  // namespace

const RodakTimeZone* TimeServiceTimeZones(size_t* count) {
    if (count != nullptr) {
        *count = sizeof(kTimeZones) / sizeof(kTimeZones[0]);
    }
    return kTimeZones;
}

const RodakNtpServer* TimeServiceNtpServers(size_t* count) {
    if (count != nullptr) {
        *count = sizeof(kNtpServers) / sizeof(kNtpServers[0]);
    }
    return kNtpServers;
}

std::string TimeServiceDefaultTimeZone() {
    return kDefaultTimeZone;
}

std::string TimeServiceDefaultNtpServer() {
    return kDefaultNtpServer;
}

std::string TimeServiceLoadTimeZone() {
    Settings settings(kClockNamespace, false);
    return settings.GetString(kTimeZoneKey, kDefaultTimeZone);
}

std::string TimeServiceLoadNtpServer() {
    Settings settings(kClockNamespace, false);
    return settings.GetString(kNtpServerKey, kDefaultNtpServer);
}

bool TimeServiceSaveTimeZone(const std::string& tz) {
    Settings settings(kClockNamespace, true);
    settings.SetString(kTimeZoneKey, tz);
    return true;
}

bool TimeServiceSaveNtpServer(const std::string& server) {
    Settings settings(kClockNamespace, true);
    settings.SetString(kNtpServerKey, server);
    return true;
}

void TimeServiceApplyTimeZone(const std::string& tz) {
    setenv("TZ", tz.c_str(), 1);
    tzset();
    ESP_LOGI(TAG, "Applied time zone: %s", tz.c_str());
}

void TimeServiceApplySavedTimeZone() {
    TimeServiceApplyTimeZone(TimeServiceLoadTimeZone());
}

bool TimeServiceStartSync(const std::string& server) {
    if (server.empty()) {
        ESP_LOGW(TAG, "Cannot start SNTP with empty server");
        return false;
    }

    TimeServiceApplySavedTimeZone();
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    ConfigureSntpServers(server);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP sync started with %s", g_active_sntp_server);
    return true;
}

bool TimeServiceStartSavedSync() {
    return TimeServiceStartSync(TimeServiceLoadNtpServer());
}

TimeSyncStatus TimeServiceGetSyncStatus() {
    const sntp_sync_status_t status = esp_sntp_get_sync_status();
    switch (status) {
        case SNTP_SYNC_STATUS_RESET:
            return TimeSyncStatus::kReset;
        case SNTP_SYNC_STATUS_IN_PROGRESS:
            return TimeSyncStatus::kInProgress;
        case SNTP_SYNC_STATUS_COMPLETED:
            return TimeSyncStatus::kCompleted;
        default:
            return TimeSyncStatus::kReset;
    }
}

size_t TimeServiceFindTimeZoneIndex(const std::string& tz) {
    size_t count = 0;
    const auto* zones = TimeServiceTimeZones(&count);
    for (size_t i = 0; i < count; ++i) {
        if (tz == zones[i].tz) {
            return i;
        }
    }
    return 0;
}

size_t TimeServiceFindNtpServerIndex(const std::string& server) {
    size_t count = 0;
    const auto* servers = TimeServiceNtpServers(&count);
    for (size_t i = 0; i < count; ++i) {
        if (server == servers[i].server) {
            return i;
        }
    }
    return (count > 0) ? count - 1 : 0;
}

bool TimeServiceTimeIsValid() {
    std::time_t now = std::time(nullptr);
    std::tm timeinfo = {};
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_year >= 120;
}
