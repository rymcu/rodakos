#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rodakos {

inline constexpr const char* kOtaDirectory = "/rodak-ota";
inline constexpr const char* kOtaPendingImagePath = "/rodak-ota/pending.bin";
inline constexpr const char* kOtaPendingPartPath = "/rodak-ota/pending.bin.part";
inline constexpr const char* kOtaInstalledImagePath = "/rodak-ota/installed.bin";
inline constexpr const char* kOtaInstalledPartPath = "/rodak-ota/installed.bin.part";
inline constexpr size_t kOtaTaskNoMaxBytes = 95;
inline constexpr size_t kOtaVersionMaxBytes = 127;
inline constexpr size_t kOtaDetailMaxBytes = 255;
inline constexpr size_t kOtaErrorCodeMaxBytes = 63;
// 日常 OTA 不更新 Recovery；只有有线迁移同时更新两端后才能升级此 wire 版本。
inline constexpr uint16_t kOtaJournalSchemaVersion = 1;

enum class OtaUpdatePhase {
    kIdle,
    kPending,
    kApplying,
    kReadyToBoot,
    kRestoring,
    kRollbackReadyToBoot,
    kRollbackBooting,
    kConfirmed,
    kRollbackPending,
    kFailed,
    kReportAcknowledged,
};

enum class OtaUpdateLoadResult {
    kEmpty,
    kValid,
    kCorrupt,
    kError,
};

struct OtaUpdateRecord {
    OtaUpdatePhase phase = OtaUpdatePhase::kIdle;
    std::string task_no;
    std::string target_version;
    uint64_t pending_size = 0;
    std::string pending_sha256;
    uint64_t installed_size = 0;
    std::string installed_sha256;
    std::string detail;
    std::string error_code;
};

const char* OtaUpdatePhaseName(OtaUpdatePhase phase);
bool InitializeOtaUpdateState();
OtaUpdateLoadResult LoadOtaUpdateRecordStatus(OtaUpdateRecord& record);
bool LoadOtaUpdateRecord(OtaUpdateRecord& record);
bool SaveOtaUpdateRecord(const OtaUpdateRecord& record);
bool ClearOtaUpdateRecord();

}  // namespace rodakos
