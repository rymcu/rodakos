#include "phone_os/battery_level_model.h"

#include <cstddef>

namespace rodakos {
namespace {
constexpr int kBatteryDividerRatio = 2;

struct BatteryCurvePoint {
    int millivolts;
    int level_percent;
};

constexpr BatteryCurvePoint kBatteryDischargeCurve[] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3920, 70},
    {3850, 60},  {3780, 50}, {3720, 40}, {3660, 30},
    {3580, 20},  {3480, 10}, {3350, 5},  {3200, 0},
};
}  // namespace

int BatteryVoltageMillivoltsFromAdc(int adc_millivolts) {
    return adc_millivolts >= 0 ? adc_millivolts * kBatteryDividerRatio : -1;
}

int EstimateBatteryLevelPercent(int battery_millivolts) {
    if (battery_millivolts >= kBatteryDischargeCurve[0].millivolts) {
        return kBatteryDischargeCurve[0].level_percent;
    }
    constexpr size_t kPointCount =
        sizeof(kBatteryDischargeCurve) / sizeof(kBatteryDischargeCurve[0]);
    for (size_t i = 0; i + 1 < kPointCount; ++i) {
        const auto& high = kBatteryDischargeCurve[i];
        const auto& low = kBatteryDischargeCurve[i + 1];
        if (battery_millivolts <= high.millivolts &&
            battery_millivolts >= low.millivolts) {
            const int voltage_range = high.millivolts - low.millivolts;
            const int level_range = high.level_percent - low.level_percent;
            return low.level_percent +
                   (battery_millivolts - low.millivolts) * level_range / voltage_range;
        }
    }
    return kBatteryDischargeCurve[kPointCount - 1].level_percent;
}

}  // namespace rodakos
