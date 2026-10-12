#pragma once

#include <cstdint>

namespace rodakos {

// Release-test only: the next `skip` LVGL-task heap allocations pass, then `count` fail.
void ArmLvglAllocationFailure(uint32_t skip, uint32_t count);

}  // namespace rodakos
