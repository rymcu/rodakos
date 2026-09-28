#include "test_framework.h"
#include "rodak_ota_signature.h"
#include "fixtures.h"

namespace {
rodakos::OtaUpdateRecord Record() {
    rodakos::OtaUpdateRecord value;
    value.task_no = "test-ota";
    value.target_version = "1.0";
    value.pending_size = 13000;
    value.pending_sha256 = kDigest;
    return value;
}
std::string File(const char* name) { return std::string(kFixtureDir) + "/" + name; }
}

RODAK_TEST("firmware verifier accepts a real signature repeatedly") {
    for (int i = 0; i < 20; ++i) {
        RODAK_CHECK(rodakos::VerifyOtaSignature(Record(), kSignature));
    }
}
RODAK_TEST("firmware verifier rejects wrong key and changed signature") {
    RODAK_CHECK_FALSE(rodakos::VerifyOtaSignature(Record(), kWrongSignature));
    std::string changed = kSignature;
    changed[0] = changed[0] == '0' ? '1' : '0';
    RODAK_CHECK_FALSE(rodakos::VerifyOtaSignature(Record(), changed));
    RODAK_CHECK_FALSE(rodakos::VerifyOtaSignature(Record(), ""));
}
RODAK_TEST("signed metadata cannot change task version size or digest") {
    auto record = Record(); record.task_no += "x";
    RODAK_CHECK_FALSE(rodakos::VerifyOtaSignature(record, kSignature));
    record = Record(); record.target_version += "x";
    RODAK_CHECK_FALSE(rodakos::VerifyOtaSignature(record, kSignature));
    record = Record(); record.pending_size++;
    RODAK_CHECK_FALSE(rodakos::VerifyOtaSignature(record, kSignature));
    record = Record(); record.pending_sha256[0] = 'f';
    RODAK_CHECK_FALSE(rodakos::VerifyOtaSignature(record, kSignature));
}
RODAK_TEST("payload rejects delimiters NUL oversize and nonhex digests") {
    auto record = Record(); record.task_no += "\n";
    RODAK_CHECK(rodakos::BuildOtaSignaturePayload(record).empty());
    record = Record(); record.target_version.push_back('\0');
    RODAK_CHECK(rodakos::BuildOtaSignaturePayload(record).empty());
    record = Record(); record.pending_size = 0xd50001;
    RODAK_CHECK(rodakos::BuildOtaSignaturePayload(record).empty());
    record = Record(); record.pending_sha256[0] = 'g';
    RODAK_CHECK(rodakos::BuildOtaSignaturePayload(record).empty());
}
RODAK_TEST("image verifier rejects tampering truncation wrong size and trailing bytes") {
    RODAK_CHECK(rodakos::VerifyOtaImageFile(File("valid.bin"), 13000, kDigest));
    for (const char* name : {"truncated.bin", "tampered.bin", "extra.bin", "absent.bin"}) {
        RODAK_CHECK_FALSE(rodakos::VerifyOtaImageFile(File(name), 13000, kDigest));
    }
    RODAK_CHECK_FALSE(rodakos::VerifyOtaImageFile(File("valid.bin"), 12999, kDigest));
}
RODAK_TEST("signature sidecar reader requires exact length and successful read") {
    std::string signature;
    RODAK_CHECK(rodakos::ReadOtaSignatureFile(File("valid.sig"), signature));
    RODAK_CHECK_EQ(signature, kSignature);
    for (const char* name : {"short.sig", "extra.sig", "absent.sig"}) {
        RODAK_CHECK_FALSE(rodakos::ReadOtaSignatureFile(File(name), signature));
    }
}
