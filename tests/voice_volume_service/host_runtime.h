#pragma once
#include <functional>

namespace rodakos_test {
void ResetWorkers();
// 仅阻塞 fake 调度器中的工作线程；真实 service 的锁、队列和生命周期照常运行。
bool PauseWorkers();
void ResumeWorkers();
void JoinWorkers();
void SetAfterSemaphoreGiveHook(std::function<void()> hook);
void AfterSemaphoreGive();
}
