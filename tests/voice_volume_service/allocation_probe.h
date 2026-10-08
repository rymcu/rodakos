#pragma once

#include <cstddef>

namespace rodakos_test {
struct AllocationRequests {
    size_t count = 0;
    size_t bytes = 0;
};
void BeginAllocationProbe();
AllocationRequests EndAllocationProbe();
}
