#include "allocation_probe.h"

namespace {
thread_local bool enabled = false;
thread_local rodakos_test::AllocationRequests requests;
void Record(size_t bytes) {
    if (!enabled) return;
    ++requests.count;
    requests.bytes += bytes;
}
}

// Wrap host allocation requests without replacing its allocator or delete ABI.
extern "C" void* __real__Znwm(size_t bytes);
extern "C" void* __real__Znam(size_t bytes);
extern "C" void* __wrap__Znwm(size_t bytes) {
    Record(bytes);
    return __real__Znwm(bytes);
}
extern "C" void* __wrap__Znam(size_t bytes) {
    Record(bytes);
    return __real__Znam(bytes);
}

namespace rodakos_test {
void BeginAllocationProbe() {
    requests = {};
    enabled = true;
}
AllocationRequests EndAllocationProbe() {
    enabled = false;
    return requests;
}
}
