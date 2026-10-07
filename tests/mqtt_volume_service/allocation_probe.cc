#include "allocation_probe.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <new>

namespace {
struct Watch {
    std::atomic<const void*> live{nullptr};
    uintptr_t original = 0;
    size_t bytes = 0;
    std::atomic<unsigned> releases{0};
};
std::array<Watch, 3> watches;
thread_local size_t armed_size = 0;
thread_local bool fail_allocation = false;
thread_local mqtt_host::NothrowProbe probe{};

void ObserveDelete(void* pointer) noexcept {
    if (pointer == nullptr) return;
    for (auto& watch : watches) {
        const void* expected = pointer;
        // 消费后失活，地址复用产生的其他释放不属于原 owner。
        if (watch.live.compare_exchange_strong(expected, nullptr)) ++watch.releases;
    }
}
}

void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
    const bool matched = armed_size != 0 && armed_size == bytes;
    if (armed_size != 0) {
        ++probe.attempts;
        if (!matched) ++probe.unexpected_size_calls;
    }
    if (matched) {
        armed_size = 0;
        probe.matched_size = bytes;
        if (fail_allocation) { ++probe.failures; return nullptr; }
    }
    try {
        void* pointer = ::operator new(bytes);
        if (matched) mqtt_host::WatchDelete(2, pointer, bytes);
        return pointer;
    } catch (...) {
        return nullptr;
    }
}

extern "C" void __real__ZdlPv(void* pointer) noexcept;
extern "C" void __real__ZdlPvm(void* pointer, size_t bytes) noexcept;
extern "C" void __wrap__ZdlPv(void* pointer) noexcept {
    ObserveDelete(pointer);
    __real__ZdlPv(pointer);
}
extern "C" void __wrap__ZdlPvm(void* pointer, size_t bytes) noexcept {
    ObserveDelete(pointer);
    __real__ZdlPvm(pointer, bytes);
}

namespace mqtt_host {
void ResetAllocationProbes() {
    armed_size = 0;
    probe = {};
    for (auto& watch : watches) {
        if (watch.live.load() != nullptr) std::abort();
        watch.original = 0;
        watch.bytes = 0;
        watch.releases = 0;
    }
}
void ArmPendingAllocation(size_t bytes, bool fail) {
    armed_size = bytes;
    fail_allocation = fail;
    probe = {};
}
void DisarmPendingAllocation() { armed_size = 0; }
NothrowProbe PendingAllocationProbe() { return probe; }
void WatchDelete(size_t slot, const void* pointer, size_t bytes) {
    auto& watch = watches.at(slot);
    if (watch.live.load() != nullptr || pointer == nullptr) std::abort();
    watch.original = reinterpret_cast<uintptr_t>(pointer);
    watch.bytes = bytes;
    watch.releases = 0;
    watch.live = pointer;
}
DeleteProbe ReadDeleteProbe(size_t slot) {
    const auto& watch = watches.at(slot);
    return {watch.original, watch.bytes, watch.releases.load(), watch.live.load() != nullptr};
}
}
