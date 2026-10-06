#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <cstdint>
#include <string>

namespace fake_light {
struct Pixel { uint32_t red = 0; uint32_t green = 0; uint32_t blue = 0; };
extern std::atomic<unsigned> pixel_calls;
extern std::atomic<unsigned> refresh_calls;
extern std::atomic<unsigned> clear_calls;
extern std::atomic<unsigned> fail_pixel_call;
extern std::atomic<bool> fail_refresh;
extern std::atomic<bool> fail_clear;
extern std::atomic<bool> unavailable;
extern std::function<void()> before_refresh;
void Reset();
void SetPrimaryId(const std::string& id);
std::array<Pixel, 4> Pixels();
}
