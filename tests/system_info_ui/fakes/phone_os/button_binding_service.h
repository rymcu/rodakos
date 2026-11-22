#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace rodakos {
struct ButtonState {
    std::string id;
    std::string title;
    std::string device_name;
    uint8_t physical_index = 0;
    bool available = false;
};
class ButtonBindingService {
public:
    const std::vector<ButtonState>& ListButtons() const { return buttons; }
    std::vector<ButtonState> buttons;
};
}  // namespace rodakos
