#pragma once

#include <cstddef>
#include <string>

struct RodakTimeZone {
    const char* label;
    const char* tz;
};

struct RodakNtpServer {
    const char* label;
    const char* server;
};

enum class TimeSyncStatus {
    kReset,
    kInProgress,
    kCompleted,
};

const RodakTimeZone* TimeServiceTimeZones(size_t* count);
const RodakNtpServer* TimeServiceNtpServers(size_t* count);

std::string TimeServiceDefaultTimeZone();
std::string TimeServiceDefaultNtpServer();
std::string TimeServiceLoadTimeZone();
std::string TimeServiceLoadNtpServer();
bool TimeServiceSaveTimeZone(const std::string& tz);
bool TimeServiceSaveNtpServer(const std::string& server);
void TimeServiceApplyTimeZone(const std::string& tz);
void TimeServiceApplySavedTimeZone();
bool TimeServiceStartSync(const std::string& server);
bool TimeServiceStartSavedSync();
TimeSyncStatus TimeServiceGetSyncStatus();
size_t TimeServiceFindTimeZoneIndex(const std::string& tz);
size_t TimeServiceFindNtpServerIndex(const std::string& server);
bool TimeServiceTimeIsValid();
