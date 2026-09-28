#include "test_framework.h"
#include "rodak_ota_state.h"
#include "rodak_ota_signature.h"
#include "fixtures.h"
#include "recovery_platform.h"
#include "esp_system.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

extern "C" void app_main();
extern "C" FILE* __real_fopen(const char*, const char*);
extern "C" FILE* __wrap_fopen(const char* path, const char* mode) {
    const std::string requested(path);
    const std::string mapped = requested.rfind("/sdcard/", 0) == 0
        ? std::string(kFixtureDir) + requested.substr(7) : requested;
    return __real_fopen(mapped.c_str(), mode);
}

namespace {
std::string fault;
bool consumed = false;
rodakos::OtaUpdateRecord Record() {
    rodakos::OtaUpdateRecord record;
    record.phase = rodakos::OtaUpdatePhase::kPending;
    record.task_no = "test-ota"; record.target_version = "1.0";
    record.pending_size = record.installed_size = 13000;
    record.pending_sha256 = record.installed_sha256 = kDigest;
    return record;
}
std::filesystem::path SdFile(const char* file) { return std::filesystem::path(kFixtureDir) / "rodak-ota" / file; }
void Prepare(rodakos::OtaUpdatePhase phase = rodakos::OtaUpdatePhase::kPending) {
    fault.clear(); consumed = false; fake_nvs::Reset(); fake_recovery::Reset();
    std::filesystem::create_directories(SdFile(""));
    for (const char* name : {"pending.bin", "installed.bin"}) {
        std::filesystem::copy_file(std::filesystem::path(kFixtureDir) / "valid.bin", SdFile(name),
                                  std::filesystem::copy_options::overwrite_existing);
    }
    std::ofstream(SdFile("pending.sig")) << kSignature;
    auto record = Record(); record.phase = phase;
    RODAK_CHECK(rodakos::SaveOtaUpdateRecord(record));
}
void Boot() { try { app_main(); } catch (const SimulatedReset&) {} }
}
namespace rodakos {
void OtaFaultPoint(const char* phase) {
    if (!consumed && fault == phase) { consumed = true; throw SimulatedReset{}; }
}
}

RODAK_TEST("Recovery rejects missing wrong and tampered candidates without an erase") {
    for (int scenario = 0; scenario < 4; ++scenario) {
        Prepare();
        if (scenario == 0) std::filesystem::remove(SdFile("pending.sig"));
        if (scenario == 1) std::ofstream(SdFile("pending.sig")) << kWrongSignature;
        if (scenario == 2) std::ofstream(SdFile("pending.bin")) << "truncated";
        if (scenario == 3) std::filesystem::copy_file(std::filesystem::path(kFixtureDir) / "tampered.bin",
            SdFile("pending.bin"), std::filesystem::copy_options::overwrite_existing);
        Boot();
        RODAK_CHECK_EQ(fake_recovery::erases, 0);
        RODAK_CHECK_EQ(fake_recovery::selected, &fake_recovery::app);
        rodakos::OtaUpdateRecord record;
        RODAK_CHECK(rodakos::LoadOtaUpdateRecord(record));
        RODAK_CHECK_EQ(record.phase, rodakos::OtaUpdatePhase::kFailed);
    }
}
RODAK_TEST("Recovery applies valid image and verifies rollback backup before erase") {
    Prepare(); Boot();
    RODAK_CHECK_EQ(fake_recovery::erases, 1);
    RODAK_CHECK_EQ(fake_recovery::flashed.size(), 13000U);
    Prepare(); std::filesystem::remove(SdFile("installed.bin")); Boot();
    RODAK_CHECK_EQ(fake_recovery::erases, 0);
}
RODAK_TEST("Recovery resumes reset at candidate write and handoff boundaries") {
    for (const char* boundary : {"after_applying_state", "before_image_erase", "after_image_erase",
                                "during_image_write", "after_image_write", "after_ready_to_boot"}) {
        Prepare(); fault = boundary; Boot(); RODAK_CHECK(consumed); Boot();
        rodakos::OtaUpdateRecord record;
        RODAK_CHECK(rodakos::LoadOtaUpdateRecord(record));
        RODAK_CHECK_EQ(record.phase, rodakos::OtaUpdatePhase::kReadyToBoot);
        RODAK_CHECK_EQ(fake_recovery::selected, &fake_recovery::app);
        RODAK_CHECK_EQ(fake_recovery::flashed.size(), 13000U);
    }
}
RODAK_TEST("Recovery resumes reset at rollback write and handoff boundaries") {
    for (const char* boundary : {"after_restore_state", "before_restore_erase", "after_restore_erase",
                                "during_restore_write", "after_restore_write", "after_rollback_ready"}) {
        Prepare(rodakos::OtaUpdatePhase::kRestoring); fault = boundary; Boot();
        RODAK_CHECK(consumed); Boot();
        rodakos::OtaUpdateRecord record;
        RODAK_CHECK(rodakos::LoadOtaUpdateRecord(record));
        RODAK_CHECK_EQ(record.phase, rodakos::OtaUpdatePhase::kRollbackBooting);
        RODAK_CHECK_EQ(fake_recovery::selected, &fake_recovery::app);
    }
}
RODAK_TEST("Recovery interrupted applying with corrupt candidate restores backup") {
    Prepare(rodakos::OtaUpdatePhase::kApplying);
    std::filesystem::remove(SdFile("pending.sig")); Boot();
    RODAK_CHECK_EQ(fake_recovery::erases, 1);
    RODAK_CHECK_EQ(fake_recovery::flashed.size(), 13000U);
}
