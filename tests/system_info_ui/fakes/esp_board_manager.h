#pragma once
#include <cstring>
inline bool esp_board_manager_check_name(const char* name) { return name != nullptr && name[0] != '\0'; }
