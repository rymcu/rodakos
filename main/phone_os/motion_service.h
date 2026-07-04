#pragma once

#include <cstdint>
#include <string>

namespace rodakos {

enum class MotionStatus {
    kUnavailable,
    kReady,
    kError,
};

struct MotionVector {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct MotionSample {
    MotionStatus status = MotionStatus::kUnavailable;
    MotionVector gyro_dps;
    MotionVector accel_g;
    float roll_deg = 0.0f;
    float pitch_deg = 0.0f;
    float yaw_deg = 0.0f;
    uint64_t timestamp_us = 0;
    std::string sensor_name;
    std::string message;
};

class MotionSensorBackend {
public:
    virtual ~MotionSensorBackend() = default;
    virtual const char* name() const = 0;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool Read(MotionSample& sample) = 0;
    virtual const char* last_error() const = 0;
};

class MotionService {
public:
    explicit MotionService(MotionSensorBackend* backend = nullptr);

    void SetBackend(MotionSensorBackend* backend);
    bool Start();
    void Stop();
    bool IsAvailable() const;
    bool ReadSample(MotionSample& sample);
    const char* sensor_name() const;
    const std::string& last_error() const { return last_error_; }

private:
    MotionSensorBackend* backend_ = nullptr;
    std::string last_error_;
    uint32_t active_clients_ = 0;
};

}  // namespace rodakos
