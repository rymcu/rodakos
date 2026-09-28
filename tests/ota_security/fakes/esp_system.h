#pragma once
struct SimulatedReset {};
[[noreturn]] inline void esp_restart() { throw SimulatedReset{}; }
