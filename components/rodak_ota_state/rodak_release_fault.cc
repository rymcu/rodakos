#include "rodak_release_fault.h"

#include <cstring>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#ifndef RODAK_OTA_FAULT_INJECTION_PHASE
#define RODAK_OTA_FAULT_INJECTION_PHASE ""
#endif
#ifndef RODAK_RELEASE_TEST_ID
#define RODAK_RELEASE_TEST_ID "trial-1"
#endif

namespace rodakos {

void OtaFaultPoint(const char* phase) {
    if (RODAK_OTA_FAULT_INJECTION_PHASE[0] == '\0' ||
        std::strcmp(RODAK_OTA_FAULT_INJECTION_PHASE, phase) != 0) {
        return;
    }
    const char* trial = RODAK_RELEASE_TEST_ID ":" RODAK_OTA_FAULT_INJECTION_PHASE;
    nvs_handle_t handle = 0;
    if (nvs_open("release_test", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE("ReleaseFault", "Cannot open injection marker; reset skipped");
        return;
    }
    char consumed[160] = {};
    size_t length = sizeof(consumed);
    const esp_err_t read = nvs_get_str(handle, "consumed", consumed, &length);
    if (read == ESP_OK && std::strcmp(consumed, trial) == 0) {
        nvs_close(handle);
        return;
    }
    const bool saved = (read == ESP_OK || read == ESP_ERR_NVS_NOT_FOUND) &&
                       nvs_set_str(handle, "consumed", trial) == ESP_OK &&
                       nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    if (!saved) {
        ESP_LOGE("ReleaseFault", "Cannot persist one-shot marker; reset skipped");
        return;
    }
    ESP_LOGW("ReleaseFault", "RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE trial=%s point=%s", trial, phase);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

}  // namespace rodakos
