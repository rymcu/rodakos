#pragma once

#include <cstdint>
#include <mutex>

namespace rodakos {

struct BatterySnapshot {
    int level_percent = -1;
    int voltage_mv = -1;
    bool charging = false;
    bool charging_valid = false;
};

class BatteryStateProvider {
public:
    virtual ~BatteryStateProvider() = default;
    virtual BatterySnapshot Read() = 0;
};

/**
 * Reads the BigSmart battery divider and charge-detect ADCs declared by the
 * Board Manager. A missing or failing ADC read is represented by -1 so the
 * cloud payload can omit an unverified battery value.
 */
class BatteryMonitor final : public BatteryStateProvider {
public:
    BatteryMonitor() = default;
    ~BatteryMonitor() override;

    BatterySnapshot Read() override;

private:
    bool ReadAveragedMillivolts(const char* peripheral_name,
                                void*& calibration_handle,
                                bool& calibration_attempted,
                                int& millivolts);
    int StabilizeBatteryLevel(float estimated_level);

    bool is_charging_ = false;
    bool has_filtered_voltage_ = false;
    float filtered_battery_voltage_ = 0.0f;
    bool has_filtered_level_ = false;
    float filtered_battery_level_ = 100.0f;
    int battery_level_percent_ = -1;
    void* charge_calibration_handle_ = nullptr;
    void* voltage_calibration_handle_ = nullptr;
    bool charge_calibration_attempted_ = false;
    bool voltage_calibration_attempted_ = false;
    mutable std::mutex mutex_;
};

}  // namespace rodakos
