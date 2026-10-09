#pragma once

#include <cstdint>

namespace rodakos {

struct CameraSignalSample {
    int64_t interval_us = 0;
    int xclk_edges = 0;
    int pclk_edges = 0;
    int vsync_edges = 0;
};

class CameraSignalDiagnostics {
public:
    CameraSignalDiagnostics() = default;
    ~CameraSignalDiagnostics();
    CameraSignalDiagnostics(const CameraSignalDiagnostics&) = delete;
    CameraSignalDiagnostics& operator=(const CameraSignalDiagnostics&) = delete;

    bool Start();
    CameraSignalSample Sample(const char* phase);
    void Stop();

private:
    struct Counter;

    bool StartCounter(Counter& counter, int gpio, const char* name);
    void StopCounter(Counter& counter);
    int ReadAndClear(Counter& counter);

    Counter* counters_ = nullptr;
    int64_t last_sample_us_ = 0;
};

}  // namespace rodakos
