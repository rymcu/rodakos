#include "task_retirement_host.h"
#include <atomic>
#include <sys/time.h>

namespace wake_host {
std::atomic<int64_t> unix_us{1800000000000000};
std::atomic<unsigned> wall_reads{0};
void JoinTasks() { retirement_host::JoinTasks(); }
}
extern "C" int __wrap_gettimeofday(timeval* value, void*) {
    ++wake_host::wall_reads;
    const int64_t us = wake_host::unix_us.load();
    value->tv_sec = us / 1000000;
    value->tv_usec = us % 1000000;
    return 0;
}
