#include "test_framework.h"

#include "phone_os/battery_level_model.h"

RODAK_TEST("battery divider converts calibrated ADC millivolts") {
    RODAK_CHECK_EQ(rodakos::BatteryVoltageMillivoltsFromAdc(2050), 4100);
    RODAK_CHECK_EQ(rodakos::BatteryVoltageMillivoltsFromAdc(-1), -1);
}

RODAK_TEST("battery curve clamps its endpoints") {
    RODAK_CHECK_EQ(rodakos::EstimateBatteryLevelPercent(4300), 100);
    RODAK_CHECK_EQ(rodakos::EstimateBatteryLevelPercent(4200), 100);
    RODAK_CHECK_EQ(rodakos::EstimateBatteryLevelPercent(3200), 0);
    RODAK_CHECK_EQ(rodakos::EstimateBatteryLevelPercent(3000), 0);
}

RODAK_TEST("battery curve interpolates between calibrated voltage points") {
    RODAK_CHECK_EQ(rodakos::EstimateBatteryLevelPercent(4050), 85);
    RODAK_CHECK_EQ(rodakos::EstimateBatteryLevelPercent(3885), 65);
    RODAK_CHECK_EQ(rodakos::EstimateBatteryLevelPercent(3530), 15);
}
