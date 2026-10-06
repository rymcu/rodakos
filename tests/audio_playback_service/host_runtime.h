#pragma once
#include <atomic>
#include <functional>
namespace audio_host {
void JoinTasks();
extern std::atomic<bool> fail_task_creation;
extern std::function<void()> task_start_hook;
}
