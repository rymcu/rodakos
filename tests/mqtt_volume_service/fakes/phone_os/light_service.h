#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace rodakos {
struct RgbColor { uint8_t red = 0; uint8_t green = 0; uint8_t blue = 0; };
struct LightState { std::string id; bool enabled = false; uint8_t brightness_percent = 60; RgbColor color; };
class LightService {
public:
    bool GetLight(size_t, LightState&) const { return false; }
    bool SetState(size_t, bool, uint8_t, RgbColor) { return false; }
};
}
