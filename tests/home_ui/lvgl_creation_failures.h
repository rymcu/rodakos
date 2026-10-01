#pragma once

namespace rodakos_home_ui_test {
enum class LvglCreationFailure { kNone, kObject, kImage, kTimer };
void ArmLvglCreationFailure(LvglCreationFailure failure);
bool ConsumeLvglCreationFailure(LvglCreationFailure failure);
}
