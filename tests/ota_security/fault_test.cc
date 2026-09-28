#include "test_framework.h"
#include "rodak_release_fault.h"
#include "nvs.h"
#include "esp_system.h"

RODAK_TEST("reset injection persists consumed marker and only resets once") {
    fake_nvs::Reset();
    bool reset = false;
    try { rodakos::OtaFaultPoint("after_image_write"); } catch (const SimulatedReset&) { reset = true; }
    RODAK_CHECK(reset);
    rodakos::OtaFaultPoint("after_image_write");
    RODAK_CHECK_EQ(fake_nvs::writes, 1);
}
RODAK_TEST("injection skips reset when marker cannot be committed") {
    fake_nvs::Reset(); fake_nvs::commit_error = true;
    rodakos::OtaFaultPoint("after_image_write");
}
RODAK_TEST("other boundaries do not consume the configured reset") {
    fake_nvs::Reset();
    rodakos::OtaFaultPoint("after_restore_write");
    RODAK_CHECK_EQ(fake_nvs::writes, 0);
}
