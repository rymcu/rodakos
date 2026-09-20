#pragma once

namespace rodakos {

int BatteryVoltageMillivoltsFromAdc(int adc_millivolts);
int EstimateBatteryLevelPercent(int battery_millivolts);

}  // namespace rodakos
