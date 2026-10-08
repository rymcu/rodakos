#pragma once
#include <functional>
#include <vector>
namespace prepare_host {
void Reset(unsigned fail_creation = 0);
void ClearTrace();
void FailOpenTakeOnce();
bool OpenHeld();
unsigned LiveSemaphores();
unsigned CloudCalls();
const std::vector<bool>& PriorityOpenStates();
const std::vector<unsigned>& CloudReturnsAtPriorityReads();
extern std::function<void()> after_cloud_return;
}
