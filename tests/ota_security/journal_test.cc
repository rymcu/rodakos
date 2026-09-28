#include "test_framework.h"
#include "rodak_ota_state.h"
#include "nvs.h"

namespace rodakos { void OtaFaultPoint(const char*) {} }
namespace {
rodakos::OtaUpdateRecord Record() {
    rodakos::OtaUpdateRecord record;
    record.phase = rodakos::OtaUpdatePhase::kPending;
    record.task_no = "task-1";
    record.target_version = "v2";
    record.pending_size = record.installed_size = 1024;
    record.pending_sha256 = record.installed_sha256 = std::string(64, 'a');
    return record;
}
}

RODAK_TEST("journal keeps exact v1 ABI and chooses latest committed generation") {
    fake_nvs::Reset(); auto record = Record();
    RODAK_CHECK(rodakos::SaveOtaUpdateRecord(record));
    RODAK_CHECK_EQ(fake_nvs::blobs["record_a"].size(), 712u);
    record.phase = rodakos::OtaUpdatePhase::kApplying;
    RODAK_CHECK(rodakos::SaveOtaUpdateRecord(record));
    RODAK_CHECK(rodakos::LoadOtaUpdateRecord(record));
    RODAK_CHECK_EQ(record.phase, rodakos::OtaUpdatePhase::kApplying);
}
RODAK_TEST("journal torn new slot preserves preceding valid generation") {
    fake_nvs::Reset(); auto record = Record();
    RODAK_CHECK(rodakos::SaveOtaUpdateRecord(record));
    record.phase = rodakos::OtaUpdatePhase::kApplying;
    fake_nvs::tear_write = true;
    RODAK_CHECK_FALSE(rodakos::SaveOtaUpdateRecord(record));
    RODAK_CHECK(rodakos::LoadOtaUpdateRecord(record));
    RODAK_CHECK_EQ(record.phase, rodakos::OtaUpdatePhase::kPending);
}
RODAK_TEST("journal uncertain commit can contain next complete state") {
    fake_nvs::Reset(); auto record = Record();
    RODAK_CHECK(rodakos::SaveOtaUpdateRecord(record));
    record.phase = rodakos::OtaUpdatePhase::kReadyToBoot;
    fake_nvs::commit_error = true;
    RODAK_CHECK_FALSE(rodakos::SaveOtaUpdateRecord(record));
    RODAK_CHECK(rodakos::LoadOtaUpdateRecord(record));
    RODAK_CHECK_EQ(record.phase, rodakos::OtaUpdatePhase::kReadyToBoot);
}
RODAK_TEST("journal refuses to repair two corrupted slots or mask read errors") {
    fake_nvs::Reset(); auto record = Record();
    fake_nvs::blobs["record_a"] = {1,2}; fake_nvs::blobs["record_b"] = {3,4};
    RODAK_CHECK_EQ(rodakos::LoadOtaUpdateRecordStatus(record), rodakos::OtaUpdateLoadResult::kCorrupt);
    RODAK_CHECK_FALSE(rodakos::SaveOtaUpdateRecord(Record()));
    RODAK_CHECK_EQ(fake_nvs::writes, 0);
    fake_nvs::Reset(); RODAK_CHECK(rodakos::SaveOtaUpdateRecord(Record()));
    fake_nvs::read_error = true;
    RODAK_CHECK_EQ(rodakos::LoadOtaUpdateRecordStatus(record), rodakos::OtaUpdateLoadResult::kError);
    RODAK_CHECK_FALSE(rodakos::ClearOtaUpdateRecord());
}
RODAK_TEST("acknowledged journal survives reboot before cleanup and clear is idempotent") {
    fake_nvs::Reset(); auto record = Record(); record.phase = rodakos::OtaUpdatePhase::kReportAcknowledged;
    RODAK_CHECK(rodakos::SaveOtaUpdateRecord(record));
    RODAK_CHECK(rodakos::LoadOtaUpdateRecord(record));
    RODAK_CHECK_EQ(record.phase, rodakos::OtaUpdatePhase::kReportAcknowledged);
    RODAK_CHECK(rodakos::ClearOtaUpdateRecord());
    RODAK_CHECK(rodakos::ClearOtaUpdateRecord());
    RODAK_CHECK_EQ(rodakos::LoadOtaUpdateRecordStatus(record), rodakos::OtaUpdateLoadResult::kEmpty);
}
