#pragma once

#include "phone_os/motion_service.h"

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>

namespace rodakos {

class Qmi8658MotionSensor final : public MotionSensorBackend {
public:
    Qmi8658MotionSensor();
    ~Qmi8658MotionSensor() override;

    const char* name() const override { return "QMI8658"; }
    bool Start() override;
    void Stop() override;
    bool Read(MotionSample& sample) override;
    const char* last_error() const override { return last_error_; }

private:
    struct CachedSample {
        MotionVector gyro_dps;
        MotionVector accel_g;
        float roll_deg = 0.0f;
        float pitch_deg = 0.0f;
        float yaw_deg = 0.0f;
        uint64_t timestamp_us = 0;
        esp_err_t error = ESP_ERR_INVALID_STATE;
        bool valid = false;
    };

    esp_err_t InitializeHardware();
    void ReleaseHardware();
    bool StartTask();
    void StopTask();
    esp_err_t WriteRegister(uint8_t reg, uint8_t value);
    esp_err_t ReadRegister(uint8_t reg, uint8_t* value);
    esp_err_t ReadRegisters(uint8_t reg, uint8_t* data, size_t len);
    esp_err_t ReadHardwareSample(CachedSample& sample);
    void PollOnce();
    void UpdateRelativeYaw(float gyro_z_dps, int64_t now_us);
    void SetLastError(const char* text);
    void SetLastError(esp_err_t err, const char* context);
    static void SensorTask(void* arg);

    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2c_master_dev_handle_t i2c_dev_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    volatile bool task_running_ = false;
    bool initialized_ = false;
    float yaw_degrees_ = 0.0f;
    int64_t last_yaw_update_us_ = 0;

    StaticSemaphore_t sample_mutex_buffer_ = {};
    SemaphoreHandle_t sample_mutex_ = nullptr;
    CachedSample cached_;
    char last_error_[96] = "QMI8658 not started";
};

}  // namespace rodakos
