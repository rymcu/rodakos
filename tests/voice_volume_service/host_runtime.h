#pragma once

namespace rodakos_test {
// 仅阻塞 fake 调度器中的工作线程；真实 service 的锁、队列和生命周期照常运行。
bool PauseWorkers();
void ResumeWorkers();
void JoinWorkers();
}
