#include "phone_os/battery_monitor.h"

#include "phone_os/battery_level_model.h"

#include <algorithm>
#include <cmath>

#include <esp_log.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_board_manager_includes.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "BatteryMonitor";
constexpr const char* kChargeAdcPeripheral = "adc_battery_charge";
constexpr const char* kVoltageAdcPeripheral = "adc_battery_voltage";
constexpr float kBatteryVoltageFilterAlpha = 0.18f;
constexpr float kBatteryLevelFilterAlpha = 0.22f;
constexpr int kChargingEnterThresholdMv = 800;
constexpr int kChargingExitThresholdMv = 1200;
constexpr int kAdcSampleCount = 15;
constexpr int kAdcTrimCount = 2;

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

BatteryMonitor::~BatteryMonitor() {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (charge_calibration_handle_ != nullptr) {
        adc_cali_delete_scheme_curve_fitting(
            static_cast<adc_cali_handle_t>(charge_calibration_handle_));
    }
    if (voltage_calibration_handle_ != nullptr) {
        adc_cali_delete_scheme_curve_fitting(
            static_cast<adc_cali_handle_t>(voltage_calibration_handle_));
    }
#endif
}

bool BatteryMonitor::ReadAveragedMillivolts(const char* peripheral_name,
                                            void*& calibration_handle,
                                            bool& calibration_attempted,
                                            int& millivolts) {
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

    if (!calibration_attempted) {
        calibration_attempted = true;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t calibration_config = {
            .unit_id = config->cfg.oneshot.unit_cfg.unit_id,
            .chan = config->cfg.oneshot.channel_id,
            .atten = config->cfg.oneshot.chan_cfg.atten,
            .bitwidth = config->cfg.oneshot.chan_cfg.bitwidth,
        };
        adc_cali_handle_t created_handle = nullptr;
        const esp_err_t calibration_error =
            adc_cali_create_scheme_curve_fitting(&calibration_config, &created_handle);
        if (calibration_error == ESP_OK) {
            calibration_handle = created_handle;
            ESP_LOGI(TAG, "ADC calibration ready for %s", peripheral_name);
        } else {
            ESP_LOGW(TAG, "ADC calibration unavailable for %s: %s",
                     peripheral_name, esp_err_to_name(calibration_error));
        }
#else
        ESP_LOGW(TAG, "ADC curve-fitting calibration is unavailable for %s", peripheral_name);
#endif
    }
    if (calibration_handle == nullptr) {
        return false;
    }

    int samples[kAdcSampleCount] = {};
    int valid_count = 0;
    for (int i = 0; i < kAdcSampleCount; ++i) {
        int sample_mv = 0;
        if (adc_oneshot_get_calibrated_result(
                handle->oneshot,
                static_cast<adc_cali_handle_t>(calibration_handle),
                config->cfg.oneshot.channel_id,
                &sample_mv) == ESP_OK) {
            samples[valid_count++] = sample_mv;
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
    millivolts = static_cast<int>(sum / count);
    return true;
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

    int charge_mv = 0;
    if (ReadAveragedMillivolts(kChargeAdcPeripheral,
                               charge_calibration_handle_,
                               charge_calibration_attempted_,
                               charge_mv)) {
        is_charging_ = is_charging_ ? charge_mv < kChargingExitThresholdMv
                                    : charge_mv < kChargingEnterThresholdMv;
        snapshot.charging = is_charging_;
        snapshot.charging_valid = true;
    }

    int adc_mv = 0;
    if (ReadAveragedMillivolts(kVoltageAdcPeripheral,
                               voltage_calibration_handle_,
                               voltage_calibration_attempted_,
                               adc_mv)) {
        const float battery_voltage =
            static_cast<float>(BatteryVoltageMillivoltsFromAdc(adc_mv));
        if (!has_filtered_voltage_) {
            has_filtered_voltage_ = true;
            filtered_battery_voltage_ = battery_voltage;
        } else {
            filtered_battery_voltage_ +=
                (battery_voltage - filtered_battery_voltage_) * kBatteryVoltageFilterAlpha;
        }
        battery_level_percent_ =
            StabilizeBatteryLevel(EstimateBatteryLevelPercent(
                static_cast<int>(std::lround(filtered_battery_voltage_))));
        snapshot.level_percent = battery_level_percent_;
        snapshot.voltage_mv = static_cast<int>(std::lround(filtered_battery_voltage_));
    }
    return snapshot;
}

}  // namespace rodakos
