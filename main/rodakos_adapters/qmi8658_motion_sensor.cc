#include "rodakos_adapters/qmi8658_motion_sensor.h"

#include <esp_board_manager_defs.h>
#include <esp_board_periph.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace rodakos {
namespace {
constexpr const char* TAG = "Qmi8658Motion";
constexpr uint8_t kQmi8658Address = 0x6A;
constexpr uint8_t kQmi8658ChipId = 0x05;
constexpr uint8_t kRegChipId = 0x00;
constexpr uint8_t kRegCtrl1 = 0x02;
constexpr uint8_t kRegCtrl2 = 0x03;
constexpr uint8_t kRegCtrl3 = 0x04;
constexpr uint8_t kRegCtrl7 = 0x08;
constexpr uint8_t kRegStatus0 = 0x2E;
constexpr uint8_t kRegAxL = 0x35;
constexpr uint8_t kRegReset = 0x60;
constexpr int kI2cTimeoutMs = 100;
constexpr int kSampleIntervalMs = 100;
constexpr uint32_t kTaskStackWords = 4096;
constexpr UBaseType_t kTaskPriority = 4;
constexpr float kYawDeadZoneDps = 0.8f;
constexpr float kMaxYawDeltaSeconds = 0.2f;
constexpr float kPi = 3.14159265358979323846f;

int16_t ReadInt16Le(const uint8_t* data) {
    return static_cast<int16_t>((static_cast<uint16_t>(data[1]) << 8) | data[0]);
}

float NormalizeYaw(float yaw) {
    while (yaw > 180.0f) {
        yaw -= 360.0f;
    }
    while (yaw < -180.0f) {
        yaw += 360.0f;
    }
    return yaw;
}

}  // namespace

Qmi8658MotionSensor::Qmi8658MotionSensor() {
    sample_mutex_ = xSemaphoreCreateMutexStatic(&sample_mutex_buffer_);
}

Qmi8658MotionSensor::~Qmi8658MotionSensor() {
    Stop();
}

bool Qmi8658MotionSensor::Start() {
    if (initialized_) {
        return StartTask();
    }

    esp_err_t ret = InitializeHardware();
    if (ret != ESP_OK) {
        SetLastError(ret, "initialize QMI8658");
        ReleaseHardware();
        return false;
    }

    yaw_degrees_ = 0.0f;
    last_yaw_update_us_ = 0;
    if (sample_mutex_ != nullptr && xSemaphoreTake(sample_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        cached_ = {};
        cached_.error = ESP_ERR_INVALID_STATE;
        xSemaphoreGive(sample_mutex_);
    }

    PollOnce();
    if (!StartTask()) {
        ReleaseHardware();
        return false;
    }

    SetLastError("");
    return true;
}

void Qmi8658MotionSensor::Stop() {
    StopTask();
    ReleaseHardware();
    if (sample_mutex_ != nullptr && xSemaphoreTake(sample_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        cached_ = {};
        cached_.error = ESP_ERR_INVALID_STATE;
        xSemaphoreGive(sample_mutex_);
    }
    SetLastError("QMI8658 stopped");
}

bool Qmi8658MotionSensor::Read(MotionSample& sample) {
    if (!initialized_) {
        SetLastError("QMI8658 not started");
        return false;
    }

    CachedSample cached;
    bool copied = false;
    if (sample_mutex_ != nullptr && xSemaphoreTake(sample_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
        cached = cached_;
        copied = true;
        xSemaphoreGive(sample_mutex_);
    }

    if (!copied) {
        SetLastError("QMI8658 sample cache busy");
        return false;
    }

    if (!cached.valid) {
        if (cached.error == ESP_ERR_INVALID_STATE) {
            SetLastError("Waiting for QMI8658 sample");
        } else {
            SetLastError(cached.error, "read QMI8658 sample");
        }
        return false;
    }

    sample.gyro_dps = cached.gyro_dps;
    sample.accel_g = cached.accel_g;
    sample.roll_deg = cached.roll_deg;
    sample.pitch_deg = cached.pitch_deg;
    sample.yaw_deg = cached.yaw_deg;
    sample.timestamp_us = cached.timestamp_us;
    sample.sensor_name = name();
    sample.message = "Ready";
    SetLastError("");
    return true;
}

esp_err_t Qmi8658MotionSensor::InitializeHardware() {
    void* periph_handle = nullptr;
    esp_err_t ret = esp_board_periph_ref_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER, &periph_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get I2C master peripheral: %s", esp_err_to_name(ret));
        return ret;
    }
    i2c_bus_ = static_cast<i2c_master_bus_handle_t>(periph_handle);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = kQmi8658Address,
        .scl_speed_hz = 100000,
    };
    ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_dev_);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add QMI8658 I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t chip_id = 0;
    for (int attempt = 0; attempt < 10; ++attempt) {
        ret = ReadRegister(kRegChipId, &chip_id);
        if (ret == ESP_OK && chip_id == kQmi8658ChipId) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read QMI8658 chip id: %s", esp_err_to_name(ret));
        return ret;
    }
    if (chip_id != kQmi8658ChipId) {
        ESP_LOGW(TAG, "Unexpected QMI8658 chip id: 0x%02x", chip_id);
        return ESP_ERR_NOT_FOUND;
    }

    ret = WriteRegister(kRegReset, 0xB0);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    ret = WriteRegister(kRegCtrl1, 0x40);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = WriteRegister(kRegCtrl2, 0x95);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = WriteRegister(kRegCtrl3, 0xD5);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = WriteRegister(kRegCtrl7, 0x03);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    initialized_ = true;
    ESP_LOGI(TAG, "QMI8658 motion sensor initialized");
    return ESP_OK;
}

void Qmi8658MotionSensor::ReleaseHardware() {
    if (i2c_dev_ != nullptr) {
        if (initialized_) {
            WriteRegister(kRegCtrl7, 0x00);
        }
        esp_err_t ret = i2c_master_bus_rm_device(i2c_dev_);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove QMI8658 I2C device: %s", esp_err_to_name(ret));
        }
        i2c_dev_ = nullptr;
    }

    if (i2c_bus_ != nullptr) {
        esp_err_t ret = esp_board_periph_unref_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to release I2C master peripheral: %s", esp_err_to_name(ret));
        }
        i2c_bus_ = nullptr;
    }

    initialized_ = false;
}

bool Qmi8658MotionSensor::StartTask() {
    if (task_handle_ != nullptr) {
        return true;
    }

    task_running_ = true;
    BaseType_t ret = xTaskCreate(SensorTask, "qmi8658_motion", kTaskStackWords, this, kTaskPriority, &task_handle_);
    if (ret != pdPASS) {
        task_running_ = false;
        task_handle_ = nullptr;
        SetLastError("Failed to start QMI8658 task");
        ESP_LOGW(TAG, "Failed to start QMI8658 task");
        return false;
    }
    return true;
}

void Qmi8658MotionSensor::StopTask() {
    task_running_ = false;
    for (int i = 0; i < 30 && task_handle_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

esp_err_t Qmi8658MotionSensor::WriteRegister(uint8_t reg, uint8_t value) {
    if (i2c_dev_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(i2c_dev_, data, sizeof(data), kI2cTimeoutMs);
}

esp_err_t Qmi8658MotionSensor::ReadRegister(uint8_t reg, uint8_t* value) {
    if (i2c_dev_ == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(i2c_dev_, &reg, 1, value, 1, kI2cTimeoutMs);
}

esp_err_t Qmi8658MotionSensor::ReadRegisters(uint8_t reg, uint8_t* data, size_t len) {
    if (i2c_dev_ == nullptr || data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(i2c_dev_, &reg, 1, data, len, kI2cTimeoutMs);
}

esp_err_t Qmi8658MotionSensor::ReadHardwareSample(CachedSample& sample) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t status = 0;
    esp_err_t ret = ReadRegister(kRegStatus0, &status);
    if (ret != ESP_OK) {
        return ret;
    }
    if ((status & 0x03) == 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
        ret = ReadRegister(kRegStatus0, &status);
        if (ret != ESP_OK) {
            return ret;
        }
        if ((status & 0x03) == 0) {
            return ESP_ERR_NOT_FOUND;
        }
    }

    uint8_t raw[12] = {};
    ret = ReadRegisters(kRegAxL, raw, sizeof(raw));
    if (ret != ESP_OK) {
        return ret;
    }

    const int16_t ax = ReadInt16Le(&raw[0]);
    const int16_t ay = ReadInt16Le(&raw[2]);
    const int16_t az = ReadInt16Le(&raw[4]);
    const int16_t gx = ReadInt16Le(&raw[6]);
    const int16_t gy = ReadInt16Le(&raw[8]);
    const int16_t gz = ReadInt16Le(&raw[10]);

    int invalid_accel_count = 0;
    constexpr int16_t kInvalidPositive = 0x7FFF;
    constexpr int16_t kInvalidNegative = static_cast<int16_t>(0x8000);
    if (ax == kInvalidPositive || ax == kInvalidNegative) {
        ++invalid_accel_count;
    }
    if (ay == kInvalidPositive || ay == kInvalidNegative) {
        ++invalid_accel_count;
    }
    if (az == kInvalidPositive || az == kInvalidNegative) {
        ++invalid_accel_count;
    }
    if (invalid_accel_count >= 2 || std::abs(ax) > 30000 || std::abs(ay) > 30000 ||
        std::abs(az) > 30000) {
        ESP_LOGD(TAG, "Invalid accel sample: ax=%d ay=%d az=%d", ax, ay, az);
        return ESP_ERR_INVALID_RESPONSE;
    }

    constexpr float kAccelScale = 8.0f / 32768.0f;
    constexpr float kGyroScale = 512.0f / 32768.0f;
    sample.accel_g = MotionVector{
        .x = ax * kAccelScale,
        .y = ay * kAccelScale,
        .z = az * kAccelScale,
    };
    sample.gyro_dps = MotionVector{
        .x = gx * kGyroScale,
        .y = gy * kGyroScale,
        .z = gz * kGyroScale,
    };

    const float pitch_denominator =
        std::sqrt(sample.accel_g.y * sample.accel_g.y + sample.accel_g.z * sample.accel_g.z);
    sample.pitch_deg = pitch_denominator < 0.001f
                           ? 0.0f
                           : std::atan2(-sample.accel_g.x, pitch_denominator) * 180.0f / kPi;

    const float roll_denominator =
        std::sqrt(sample.accel_g.x * sample.accel_g.x + sample.accel_g.z * sample.accel_g.z);
    sample.roll_deg = roll_denominator < 0.001f
                          ? 0.0f
                          : std::atan2(sample.accel_g.y, roll_denominator) * 180.0f / kPi;

    const int64_t now_us = esp_timer_get_time();
    UpdateRelativeYaw(sample.gyro_dps.z, now_us);
    sample.yaw_deg = yaw_degrees_;
    sample.timestamp_us = static_cast<uint64_t>(now_us);
    sample.error = ESP_OK;
    sample.valid = true;
    return ESP_OK;
}

void Qmi8658MotionSensor::PollOnce() {
    CachedSample sample;
    esp_err_t ret = ReadHardwareSample(sample);

    if (sample_mutex_ == nullptr || xSemaphoreTake(sample_mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    if (ret == ESP_OK) {
        cached_ = sample;
    } else if (!cached_.valid) {
        cached_.error = ret;
        cached_.timestamp_us = static_cast<uint64_t>(esp_timer_get_time());
    }

    xSemaphoreGive(sample_mutex_);
}

void Qmi8658MotionSensor::UpdateRelativeYaw(float gyro_z_dps, int64_t now_us) {
    if (last_yaw_update_us_ == 0) {
        last_yaw_update_us_ = now_us;
        return;
    }

    float dt = static_cast<float>(now_us - last_yaw_update_us_) / 1000000.0f;
    last_yaw_update_us_ = now_us;
    dt = std::max(0.0f, std::min(dt, kMaxYawDeltaSeconds));
    if (std::fabs(gyro_z_dps) > kYawDeadZoneDps) {
        yaw_degrees_ = NormalizeYaw(yaw_degrees_ + gyro_z_dps * dt);
    }
}

void Qmi8658MotionSensor::SetLastError(const char* text) {
    std::snprintf(last_error_, sizeof(last_error_), "%s", text != nullptr ? text : "");
}

void Qmi8658MotionSensor::SetLastError(esp_err_t err, const char* context) {
    std::snprintf(last_error_, sizeof(last_error_), "%s: %s",
                  context != nullptr ? context : "QMI8658 error",
                  esp_err_to_name(err));
}

void Qmi8658MotionSensor::SensorTask(void* arg) {
    auto* self = static_cast<Qmi8658MotionSensor*>(arg);
    while (self != nullptr && self->task_running_) {
        self->PollOnce();
        vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
    }
    if (self != nullptr) {
        self->task_handle_ = nullptr;
    }
    vTaskDelete(nullptr);
}

}  // namespace rodakos
