#include "rodak_ota_signature.h"

#include "test_framework.h"

#include <string>

namespace {

rodakos::OtaUpdateRecord SampleRecord() {
    rodakos::OtaUpdateRecord record;
    record.task_no = "ota-task-7";
    record.target_version = "2026.09.28";
    record.pending_size = 123456;
    record.pending_sha256 = std::string(64, 'a');
    return record;
}

}  // namespace

RODAK_TEST("OTA signature payload binds task, version, size, and digest") {
    const rodakos::OtaUpdateRecord record = SampleRecord();
    RODAK_CHECK_EQ(
        rodakos::BuildOtaSignaturePayload(record),
        "rodakos-ota-v2\nrymcu-bigsmart\nota_0\nrsa2048-sha256\nota-task-7\n2026.09.28\n123456\n" + std::string(64, 'a') + "\n");
}

RODAK_TEST("OTA signature payload rejects incomplete records") {
    rodakos::OtaUpdateRecord record = SampleRecord();
    record.pending_sha256.clear();
    RODAK_CHECK(rodakos::BuildOtaSignaturePayload(record).empty());
    record = SampleRecord();
    record.pending_size = 0;
    RODAK_CHECK(rodakos::BuildOtaSignaturePayload(record).empty());
}

RODAK_TEST("OTA signature hex validation rejects malformed signatures") {
    RODAK_CHECK(rodakos::IsValidOtaSignatureHex(std::string(512, '0')));
    RODAK_CHECK_FALSE(rodakos::IsValidOtaSignatureHex(std::string(512, 'A')));
    RODAK_CHECK_FALSE(rodakos::IsValidOtaSignatureHex(std::string(510, '0')));
    std::string invalid(512, '0');
    invalid[17] = 'g';
    RODAK_CHECK_FALSE(rodakos::IsValidOtaSignatureHex(invalid));
}
