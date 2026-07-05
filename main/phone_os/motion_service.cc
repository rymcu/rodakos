#include "phone_os/motion_service.h"

#include <esp_timer.h>

namespace rodakos {
namespace {
constexpr const char* kNoMotionBackend = "Motion sensor not configured";
constexpr const char* kMotionStopped = "Motion sensor stopped";
constexpr const char* kUnknownMotionError = "Motion sample unavailable";
}  // namespace

MotionService::MotionService(MotionSensorBackend* backend) : backend_(backend) {}

void MotionService::SetBackend(MotionSensorBackend* backend) {
    if (backend_ != nullptr && active_clients_ > 0) {
        backend_->Stop();
    }
    backend_ = backend;
    active_clients_ = 0;
    last_error_.clear();
}

bool MotionService::Start() {
    if (backend_ == nullptr) {
        last_error_ = kNoMotionBackend;
        return false;
    }

    if (active_clients_ > 0) {
        ++active_clients_;
        return true;
    }

    if (!backend_->Start()) {
        const char* error = backend_->last_error();
        last_error_ = (error != nullptr && error[0] != '\0') ? error : kUnknownMotionError;
        return false;
    }

    active_clients_ = 1;
    last_error_.clear();
    return true;
}

void MotionService::Stop() {
    if (active_clients_ == 0) {
        return;
    }
    --active_clients_;
    if (active_clients_ == 0 && backend_ != nullptr) {
        backend_->Stop();
    }
}

bool MotionService::IsAvailable() const {
    return backend_ != nullptr && active_clients_ > 0;
}

bool MotionService::ReadSample(MotionSample& sample) {
    sample = {};
    sample.timestamp_us = static_cast<uint64_t>(esp_timer_get_time());

    if (backend_ == nullptr) {
        sample.status = MotionStatus::kUnavailable;
        sample.message = kNoMotionBackend;
        last_error_ = sample.message;
        return false;
    }

    if (active_clients_ == 0) {
        sample.status = MotionStatus::kUnavailable;
        sample.sensor_name = sensor_name();
        sample.message = kMotionStopped;
        last_error_ = sample.message;
        return false;
    }

    sample.sensor_name = sensor_name();
    if (backend_->Read(sample)) {
        sample.status = MotionStatus::kReady;
        if (sample.sensor_name.empty()) {
            sample.sensor_name = sensor_name();
        }
        sample.message = "Ready";
        last_error_.clear();
        return true;
    }

    const char* error = backend_->last_error();
    last_error_ = (error != nullptr && error[0] != '\0') ? error : kUnknownMotionError;
    sample.status = MotionStatus::kError;
    sample.message = last_error_;
    if (sample.sensor_name.empty()) {
        sample.sensor_name = sensor_name();
    }
    return false;
}

MotionServiceState MotionService::GetState() const {
    MotionServiceState state;
    state.backend_configured = backend_ != nullptr;
    state.active_clients = active_clients_;
    state.active = backend_ != nullptr && active_clients_ > 0;
    state.sensor_name = sensor_name();
    state.last_error = last_error_;
    return state;
}

const char* MotionService::sensor_name() const {
    if (backend_ == nullptr) {
        return "Not configured";
    }
    const char* name = backend_->name();
    return (name != nullptr && name[0] != '\0') ? name : "Motion sensor";
}

}  // namespace rodakos
