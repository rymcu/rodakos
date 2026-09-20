#include "phone_os/battery_monitor.h"

#include <algorithm>
#include <cmath>

#include <esp_log.h>
#include <esp_board_manager_includes.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "BatteryMonitor";
constexpr const char* kChargeAdcPeripheral = "adc_battery_charge";
constexpr const char* kVoltageAdcPeripheral = "adc_battery_voltage";
constexpr float kAdcReferenceVoltage = 3.3f;
constexpr float kAdcCalibrationFactor = 1.058f;
constexpr float kBatteryDividerRatio = 2.0f;
constexpr float kBatteryVoltageFilterAlpha = 0.18f;
constexpr float kBatteryLevelFilterAlpha = 0.22f;
constexpr float kChargingEnterThresholdV = 0.8f;
constexpr float kChargingExitThresholdV = 1.2f;
constexpr int kAdcSampleCount = 15;
constexpr int kAdcTrimCount = 2;

struct BatteryCurvePoint {
    float voltage;
    float level;
};

// Li-ion discharge curve used by the BigSmart board's 2:1 divider.
constexpr BatteryCurvePoint kBatteryDischargeCurve[] = {
    {4.20f, 100.0f}, {4.10f, 90.0f}, {4.00f, 80.0f}, {3.92f, 70.0f},
    {3.85f, 60.0f},  {3.78f, 50.0f}, {3.72f, 40.0f}, {3.66f, 30.0f},
    {3.58f, 20.0f},  {3.48f, 10.0f}, {3.35f, 5.0f},  {3.20f, 0.0f},
};

void SortSamples(int* samples, int count) {
    for (int i = 1; i < count; ++i) {
        const int sample = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > sample) {
            samples[j + 1] = samples[j];
            --j;
        }
        samples[j + 1] = sample;
    }
}
}  // namespace

bool BatteryMonitor::ReadAveragedAdc(const char* peripheral_name, int& reading) const {
    void* raw_handle = nullptr;
    void* raw_config = nullptr;
    if (esp_board_manager_get_periph_handle(peripheral_name, &raw_handle) != ESP_OK ||
        esp_board_manager_get_periph_config(peripheral_name, &raw_config) != ESP_OK ||
        raw_handle == nullptr || raw_config == nullptr) {
        return false;
    }

    auto* handle = static_cast<periph_adc_handle_t*>(raw_handle);
    auto* config = static_cast<periph_adc_config_t*>(raw_config);
    if (config->role != ESP_BOARD_PERIPH_ROLE_ONESHOT || handle->oneshot == nullptr) {
        return false;
    }

    int samples[kAdcSampleCount] = {};
    int valid_count = 0;
    for (int i = 0; i < kAdcSampleCount; ++i) {
        int sample = 0;
        if (adc_oneshot_read(handle->oneshot, config->cfg.oneshot.channel_id, &sample) == ESP_OK) {
            samples[valid_count++] = sample;
        }
    }
    if (valid_count == 0) {
        ESP_LOGW(TAG, "ADC read failed for %s", peripheral_name);
        return false;
    }

    SortSamples(samples, valid_count);
    const int trim = valid_count > kAdcTrimCount * 2 ? kAdcTrimCount : 0;
    int64_t sum = 0;
    int count = 0;
    for (int i = trim; i < valid_count - trim; ++i) {
        sum += samples[i];
        ++count;
    }
    reading = static_cast<int>(sum / count);
    return true;
}

int BatteryMonitor::EstimateBatteryLevel(float battery_voltage) const {
    if (battery_voltage >= kBatteryDischargeCurve[0].voltage) {
        return static_cast<int>(kBatteryDischargeCurve[0].level);
    }
    constexpr size_t kPointCount =
        sizeof(kBatteryDischargeCurve) / sizeof(kBatteryDischargeCurve[0]);
    for (size_t i = 0; i + 1 < kPointCount; ++i) {
        const auto& high = kBatteryDischargeCurve[i];
        const auto& low = kBatteryDischargeCurve[i + 1];
        if (battery_voltage <= high.voltage && battery_voltage >= low.voltage) {
            const float voltage_range = high.voltage - low.voltage;
            const float level_range = high.level - low.level;
            return static_cast<int>(low.level +
                                    (battery_voltage - low.voltage) * level_range / voltage_range);
        }
    }
    return static_cast<int>(kBatteryDischargeCurve[kPointCount - 1].level);
}

int BatteryMonitor::StabilizeBatteryLevel(float estimated_level) {
    estimated_level = std::clamp(estimated_level, 0.0f, 100.0f);
    if (!has_filtered_level_) {
        has_filtered_level_ = true;
        filtered_battery_level_ = estimated_level;
    } else {
        float next_level = filtered_battery_level_ +
                           (estimated_level - filtered_battery_level_) * kBatteryLevelFilterAlpha;
        next_level = is_charging_ ? std::max(next_level, filtered_battery_level_)
                                  : std::min(next_level, filtered_battery_level_);
        filtered_battery_level_ = std::clamp(next_level, 0.0f, 100.0f);
    }
    return static_cast<int>(std::lround(filtered_battery_level_));
}

BatterySnapshot BatteryMonitor::Read() {
    std::lock_guard<std::mutex> lock(mutex_);
    BatterySnapshot snapshot;

    int charge_raw = 0;
    if (ReadAveragedAdc(kChargeAdcPeripheral, charge_raw)) {
        const float charge_voltage =
            static_cast<float>(charge_raw) * kAdcReferenceVoltage / 4095.0f;
        is_charging_ = is_charging_ ? charge_voltage < kChargingExitThresholdV
                                    : charge_voltage < kChargingEnterThresholdV;
        snapshot.charging = is_charging_;
        snapshot.charging_valid = true;
    }

    int voltage_raw = 0;
    if (ReadAveragedAdc(kVoltageAdcPeripheral, voltage_raw)) {
        const float adc_voltage = static_cast<float>(voltage_raw) * kAdcReferenceVoltage / 4095.0f;
        const float battery_voltage = adc_voltage * kAdcCalibrationFactor * kBatteryDividerRatio;
        if (!has_filtered_voltage_) {
            has_filtered_voltage_ = true;
            filtered_battery_voltage_ = battery_voltage;
        } else {
            filtered_battery_voltage_ +=
                (battery_voltage - filtered_battery_voltage_) * kBatteryVoltageFilterAlpha;
        }
        battery_level_percent_ =
            StabilizeBatteryLevel(EstimateBatteryLevel(filtered_battery_voltage_));
        snapshot.level_percent = battery_level_percent_;
        snapshot.voltage_mv = static_cast<int>(std::lround(filtered_battery_voltage_ * 1000.0f));
    }
    return snapshot;
}

}  // namespace rodakos
