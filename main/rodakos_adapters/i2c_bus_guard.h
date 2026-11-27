#pragma once

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#endif

namespace rodakos {

// Serializes continuous i2c_master bus polling (GT911 touch) against runtime
// i2c_master_bus_rm_device() calls (Camera DVP SCCB, QMI8658, Audio ADC).
// ESP-IDF 6.0.2 checks bus status > I2C_STATUS_START before taking bus_lock_mux
// inside i2c_master_bus_rm_device(), so any concurrent transfer fails with
// ESP_ERR_INVALID_STATE. Always treat this as a leaf lock.
class I2cBusAccessLock {
public:
    I2cBusAccessLock() {
#ifdef ESP_PLATFORM
        xSemaphoreTake(SharedMutex(), portMAX_DELAY);
#else
        SharedMutex().lock();
#endif
    }

    ~I2cBusAccessLock() {
#ifdef ESP_PLATFORM
        xSemaphoreGive(SharedMutex());
#else
        SharedMutex().unlock();
#endif
    }

    I2cBusAccessLock(const I2cBusAccessLock&) = delete;
    I2cBusAccessLock& operator=(const I2cBusAccessLock&) = delete;

private:
#ifdef ESP_PLATFORM
    static SemaphoreHandle_t SharedMutex() {
        static StaticSemaphore_t storage;
        static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&storage);
        return mutex;
    }
#else
    static std::mutex& SharedMutex() {
        static std::mutex mutex;
        return mutex;
    }
#endif
};

}  // namespace rodakos
