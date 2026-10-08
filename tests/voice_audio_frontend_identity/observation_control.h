#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rodakos_test::afe_observation {
enum class SemaphorePoint { kBeforeTake, kAfterTake, kAfterGive };
void Reset();
void ArmSemaphore(void* semaphore, const char* task, SemaphorePoint point,
                  std::function<bool()> predicate = {});
bool WaitSemaphoreBlocked();
void ReleaseSemaphore();
void OnSemaphore(void* semaphore, SemaphorePoint point);
void ArmLog(const std::string& contains);
bool WaitLogBlocked();
void ReleaseLog();
void ReleaseAll();
void ArmCritical(void* mux, const char* task);
bool WaitCriticalBlocked();
void ReleaseCritical();
void OnCriticalEnter(void* mux);
void OnLog(const std::string& message);
bool WaitLogContaining(const std::string& contains);
std::vector<std::string> Logs();
int64_t NowUs();
void AdvanceUs(int64_t amount);
}
