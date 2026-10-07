#pragma once

#include <cstddef>
#include <cstdint>

namespace mqtt_host {
struct NothrowProbe {
    size_t attempts;
    size_t matched_size;
    size_t unexpected_size_calls;
    size_t failures;
};
struct DeleteProbe {
    uintptr_t original_pointer;
    size_t bytes;
    unsigned releases;
    bool live;
};
void ResetAllocationProbes();
void ArmPendingAllocation(size_t bytes, bool fail);
void DisarmPendingAllocation();
NothrowProbe PendingAllocationProbe();
void WatchDelete(size_t slot, const void* pointer, size_t bytes);
DeleteProbe ReadDeleteProbe(size_t slot);
}
