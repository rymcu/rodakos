#include "rodak_ota_state.h"

#include <nvs.h>
#include <nvs_flash.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

namespace rodakos {
namespace {
constexpr const char* kPartition = "ota_state";
constexpr const char* kNamespace = "rodak_ota";
constexpr const char* kSlotAKey = "record_a";
constexpr const char* kSlotBKey = "record_b";
constexpr uint32_t kRecordMagic = 0x524f5441;

enum class PersistedPhase : uint8_t {
    kIdle = 0x00,
    kPending = 0x10,
    kApplying = 0x11,
    kReadyToBoot = 0x12,
    kRestoring = 0x20,
    kRollbackReadyToBoot = 0x21,
    kRollbackBooting = 0x22,
    kConfirmed = 0x30,
    kRollbackPending = 0x31,
    kFailed = 0x40,
    kReportAcknowledged = 0x50,
};

struct PersistedRecord {
    uint32_t magic = kRecordMagic;
    uint16_t schema_version = kOtaJournalSchemaVersion;
    uint8_t phase = 0;
    uint8_t reserved = 0;
    uint32_t generation = 0;
    uint32_t reserved_alignment = 0;
    uint64_t pending_size = 0;
    uint64_t installed_size = 0;
    char task_no[kOtaTaskNoMaxBytes + 1] = {};
    char target_version[kOtaVersionMaxBytes + 1] = {};
    char pending_sha256[65] = {};
    char installed_sha256[65] = {};
    char detail[kOtaDetailMaxBytes + 1] = {};
    char error_code[kOtaErrorCodeMaxBytes + 1] = {};
    uint8_t reserved_tail[2] = {};
    uint32_t crc32 = 0;
};

static_assert(offsetof(PersistedRecord, generation) == 8);
static_assert(offsetof(PersistedRecord, pending_size) == 16);
static_assert(offsetof(PersistedRecord, task_no) == 32);
static_assert(offsetof(PersistedRecord, crc32) == 708);
static_assert(sizeof(PersistedRecord) == 712);

enum class SlotStatus {
    kMissing,
    kValid,
    kCorrupt,
    kError,
};

struct SlotRecord {
    SlotStatus status = SlotStatus::kMissing;
    uint32_t generation = 0;
    OtaUpdateRecord record;
};

uint32_t CalculateCrc32(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

bool IsSha256(const std::string& value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
               return std::isxdigit(ch) != 0;
           });
}

bool ValidateRecord(const OtaUpdateRecord& record) {
    const bool has_pending = record.pending_size > 0 && IsSha256(record.pending_sha256);
    const bool has_installed = record.installed_size > 0 && IsSha256(record.installed_sha256);
    switch (record.phase) {
        case OtaUpdatePhase::kPending:
        case OtaUpdatePhase::kApplying:
        case OtaUpdatePhase::kReadyToBoot:
        case OtaUpdatePhase::kConfirmed:
            return !record.task_no.empty() && has_pending && has_installed;
        case OtaUpdatePhase::kRestoring:
        case OtaUpdatePhase::kRollbackReadyToBoot:
        case OtaUpdatePhase::kRollbackBooting:
        case OtaUpdatePhase::kRollbackPending:
            return !record.task_no.empty() && has_installed;
        case OtaUpdatePhase::kFailed:
            return !record.task_no.empty() && !record.error_code.empty();
        case OtaUpdatePhase::kReportAcknowledged:
            return !record.task_no.empty();
        case OtaUpdatePhase::kIdle:
            return true;
    }
    return false;
}

bool EncodePhase(OtaUpdatePhase source, uint8_t& destination) {
    switch (source) {
        case OtaUpdatePhase::kIdle: destination = static_cast<uint8_t>(PersistedPhase::kIdle); break;
        case OtaUpdatePhase::kPending: destination = static_cast<uint8_t>(PersistedPhase::kPending); break;
        case OtaUpdatePhase::kApplying: destination = static_cast<uint8_t>(PersistedPhase::kApplying); break;
        case OtaUpdatePhase::kReadyToBoot: destination = static_cast<uint8_t>(PersistedPhase::kReadyToBoot); break;
        case OtaUpdatePhase::kRestoring: destination = static_cast<uint8_t>(PersistedPhase::kRestoring); break;
        case OtaUpdatePhase::kRollbackReadyToBoot: destination = static_cast<uint8_t>(PersistedPhase::kRollbackReadyToBoot); break;
        case OtaUpdatePhase::kRollbackBooting: destination = static_cast<uint8_t>(PersistedPhase::kRollbackBooting); break;
        case OtaUpdatePhase::kConfirmed: destination = static_cast<uint8_t>(PersistedPhase::kConfirmed); break;
        case OtaUpdatePhase::kRollbackPending: destination = static_cast<uint8_t>(PersistedPhase::kRollbackPending); break;
        case OtaUpdatePhase::kFailed: destination = static_cast<uint8_t>(PersistedPhase::kFailed); break;
        case OtaUpdatePhase::kReportAcknowledged: destination = static_cast<uint8_t>(PersistedPhase::kReportAcknowledged); break;
        default: return false;
    }
    return true;
}

bool DecodePhase(uint8_t source, OtaUpdatePhase& destination) {
    switch (static_cast<PersistedPhase>(source)) {
        case PersistedPhase::kIdle: destination = OtaUpdatePhase::kIdle; return true;
        case PersistedPhase::kPending: destination = OtaUpdatePhase::kPending; return true;
        case PersistedPhase::kApplying: destination = OtaUpdatePhase::kApplying; return true;
        case PersistedPhase::kReadyToBoot: destination = OtaUpdatePhase::kReadyToBoot; return true;
        case PersistedPhase::kRestoring: destination = OtaUpdatePhase::kRestoring; return true;
        case PersistedPhase::kRollbackReadyToBoot: destination = OtaUpdatePhase::kRollbackReadyToBoot; return true;
        case PersistedPhase::kRollbackBooting: destination = OtaUpdatePhase::kRollbackBooting; return true;
        case PersistedPhase::kConfirmed: destination = OtaUpdatePhase::kConfirmed; return true;
        case PersistedPhase::kRollbackPending: destination = OtaUpdatePhase::kRollbackPending; return true;
        case PersistedPhase::kFailed: destination = OtaUpdatePhase::kFailed; return true;
        case PersistedPhase::kReportAcknowledged: destination = OtaUpdatePhase::kReportAcknowledged; return true;
    }
    return false;
}

template <size_t N>
bool EncodeString(const std::string& value, char (&destination)[N]) {
    if (value.size() >= N) {
        return false;
    }
    std::memcpy(destination, value.data(), value.size());
    destination[value.size()] = '\0';
    return true;
}

template <size_t N>
bool DecodeString(const char (&source)[N], std::string& destination) {
    const void* terminator = std::memchr(source, '\0', N);
    if (terminator == nullptr) {
        return false;
    }
    destination.assign(source, static_cast<const char*>(terminator) - source);
    return true;
}

bool EncodeRecord(const OtaUpdateRecord& source, uint32_t generation,
                  PersistedRecord& destination) {
    if (!ValidateRecord(source)) {
        return false;
    }
    destination = {};
    if (!EncodePhase(source.phase, destination.phase)) {
        return false;
    }
    destination.generation = generation;
    destination.pending_size = source.pending_size;
    destination.installed_size = source.installed_size;
    if (!EncodeString(source.task_no, destination.task_no) ||
        !EncodeString(source.target_version, destination.target_version) ||
        !EncodeString(source.pending_sha256, destination.pending_sha256) ||
        !EncodeString(source.installed_sha256, destination.installed_sha256) ||
        !EncodeString(source.detail, destination.detail) ||
        !EncodeString(source.error_code, destination.error_code)) {
        return false;
    }
    destination.crc32 = 0;
    destination.crc32 = CalculateCrc32(&destination, sizeof(destination));
    return true;
}

bool DecodeRecord(const PersistedRecord& source, OtaUpdateRecord& destination) {
    destination = {};
    if (!DecodePhase(source.phase, destination.phase)) {
        return false;
    }
    destination.pending_size = source.pending_size;
    destination.installed_size = source.installed_size;
    if (!DecodeString(source.task_no, destination.task_no) ||
        !DecodeString(source.target_version, destination.target_version) ||
        !DecodeString(source.pending_sha256, destination.pending_sha256) ||
        !DecodeString(source.installed_sha256, destination.installed_sha256) ||
        !DecodeString(source.detail, destination.detail) ||
        !DecodeString(source.error_code, destination.error_code)) {
        return false;
    }
    return ValidateRecord(destination);
}

SlotRecord ReadSlot(nvs_handle_t handle, const char* key) {
    SlotRecord result;
    size_t size = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return result;
    }
    if (err != ESP_OK) {
        result.status = SlotStatus::kError;
        return result;
    }
    if (size != sizeof(PersistedRecord)) {
        result.status = SlotStatus::kCorrupt;
        return result;
    }

    PersistedRecord persisted = {};
    err = nvs_get_blob(handle, key, &persisted, &size);
    if (err != ESP_OK) {
        result.status = SlotStatus::kError;
        return result;
    }
    const uint32_t expected_crc = persisted.crc32;
    persisted.crc32 = 0;
    if (persisted.magic != kRecordMagic ||
        persisted.schema_version != kOtaJournalSchemaVersion ||
        CalculateCrc32(&persisted, sizeof(persisted)) != expected_crc ||
        !DecodeRecord(persisted, result.record)) {
        result.status = SlotStatus::kCorrupt;
        return result;
    }
    result.status = SlotStatus::kValid;
    result.generation = persisted.generation;
    return result;
}

bool IsNewerGeneration(uint32_t candidate, uint32_t baseline) {
    return static_cast<int32_t>(candidate - baseline) > 0;
}
}  // namespace

const char* OtaUpdatePhaseName(OtaUpdatePhase phase) {
    switch (phase) {
        case OtaUpdatePhase::kPending: return "pending";
        case OtaUpdatePhase::kApplying: return "applying";
        case OtaUpdatePhase::kReadyToBoot: return "ready_to_boot";
        case OtaUpdatePhase::kRestoring: return "restoring";
        case OtaUpdatePhase::kRollbackReadyToBoot: return "rollback_ready";
        case OtaUpdatePhase::kRollbackBooting: return "rollback_booting";
        case OtaUpdatePhase::kConfirmed: return "confirmed";
        case OtaUpdatePhase::kRollbackPending: return "rollback";
        case OtaUpdatePhase::kFailed: return "failed";
        case OtaUpdatePhase::kReportAcknowledged: return "report_acknowledged";
        case OtaUpdatePhase::kIdle: return "idle";
    }
    return "idle";
}

bool InitializeOtaUpdateState() {
    return nvs_flash_init_partition(kPartition) == ESP_OK;
}

OtaUpdateLoadResult LoadOtaUpdateRecordStatus(OtaUpdateRecord& record) {
    if (!InitializeOtaUpdateState()) {
        record = {};
        return OtaUpdateLoadResult::kError;
    }
    nvs_handle_t handle = 0;
    const esp_err_t open_err =
        nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle);
    if (open_err == ESP_ERR_NVS_NOT_FOUND) {
        record = {};
        return OtaUpdateLoadResult::kEmpty;
    }
    if (open_err != ESP_OK) {
        record = {};
        return OtaUpdateLoadResult::kError;
    }

    const SlotRecord slot_a = ReadSlot(handle, kSlotAKey);
    const SlotRecord slot_b = ReadSlot(handle, kSlotBKey);
    nvs_close(handle);
    const SlotRecord* selected = nullptr;
    if (slot_a.status == SlotStatus::kValid) {
        selected = &slot_a;
    }
    if (slot_b.status == SlotStatus::kValid &&
        (selected == nullptr || IsNewerGeneration(slot_b.generation, selected->generation))) {
        selected = &slot_b;
    }
    if (selected != nullptr) {
        record = selected->record;
        return record.phase == OtaUpdatePhase::kIdle ? OtaUpdateLoadResult::kEmpty
                                                     : OtaUpdateLoadResult::kValid;
    }

    record = {};
    if (slot_a.status == SlotStatus::kError || slot_b.status == SlotStatus::kError) {
        return OtaUpdateLoadResult::kError;
    }
    if (slot_a.status == SlotStatus::kCorrupt || slot_b.status == SlotStatus::kCorrupt) {
        return OtaUpdateLoadResult::kCorrupt;
    }
    return OtaUpdateLoadResult::kEmpty;
}

bool LoadOtaUpdateRecord(OtaUpdateRecord& record) {
    return LoadOtaUpdateRecordStatus(record) == OtaUpdateLoadResult::kValid;
}

bool SaveOtaUpdateRecord(const OtaUpdateRecord& record) {
    if (!InitializeOtaUpdateState()) {
        return false;
    }
    nvs_handle_t handle = 0;
    if (nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    const SlotRecord slot_a = ReadSlot(handle, kSlotAKey);
    const SlotRecord slot_b = ReadSlot(handle, kSlotBKey);
    uint32_t latest_generation = 0;
    bool has_generation = false;
    if (slot_a.status == SlotStatus::kValid) {
        latest_generation = slot_a.generation;
        has_generation = true;
    }
    if (slot_b.status == SlotStatus::kValid &&
        (!has_generation || IsNewerGeneration(slot_b.generation, latest_generation))) {
        latest_generation = slot_b.generation;
        has_generation = true;
    }

    const char* target_key = kSlotAKey;
    if (slot_a.status == SlotStatus::kValid && slot_b.status != SlotStatus::kValid) {
        target_key = kSlotBKey;
    } else if (slot_a.status == SlotStatus::kValid && slot_b.status == SlotStatus::kValid) {
        target_key = IsNewerGeneration(slot_a.generation, slot_b.generation) ? kSlotBKey
                                                                            : kSlotAKey;
    }

    PersistedRecord persisted = {};
    const uint32_t next_generation = has_generation ? latest_generation + 1 : 1;
    const bool encoded = EncodeRecord(record, next_generation, persisted);
    const bool saved = encoded &&
                       nvs_set_blob(handle, target_key, &persisted, sizeof(persisted)) == ESP_OK &&
                       nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return saved;
}

bool ClearOtaUpdateRecord() {
    return SaveOtaUpdateRecord({});
}

}  // namespace rodakos
