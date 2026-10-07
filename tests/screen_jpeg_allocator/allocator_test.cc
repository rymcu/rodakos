#include "host_heap.h"
#include "screen_jpeg_allocation.h"
#include <esp_heap_caps.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <latch>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
using rodakos::ScreenJpegAllocationScope;
constexpr unsigned kExternal = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr unsigned kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)
struct Deleter { void operator()(void* p) const { jpeg_free(p); } };
using Owned = std::unique_ptr<void, Deleter>;
void CheckZero(void* pointer, size_t count) {
    CHECK(pointer != nullptr);
    for (size_t i = 0; i < count; ++i) CHECK(static_cast<uint8_t*>(pointer)[i] == 0);
}
void CheckExternal(void* pointer, size_t size, size_t alignment = 1) {
    CheckZero(pointer, size);
    CHECK(AllocationCaps(pointer) == kExternal);
    CHECK(reinterpret_cast<uintptr_t>(pointer) % alignment == 0);
}
void AllPassthrough() {
    Owned a(jpeg_calloc(3, 19)), b(jpeg_calloc_inner(71));
    Owned c(jpeg_calloc_align(93, 32)), d(jpeg_calloc_align_inner(127, 64));
    CheckExternal(a.get(), 57);
    CheckZero(b.get(), 71);
    CheckExternal(c.get(), 93, 32);
    CheckZero(d.get(), 127);
    CHECK(AllocationCaps(b.get()) == kInternal);
    CHECK(AllocationCaps(d.get()) == kInternal);
    CHECK(reinterpret_cast<uintptr_t>(d.get()) % 64 == 0);
    const auto stats = GetHeapStats();
    for (auto count : stats.real_calls) CHECK(count == 1);
    CHECK(stats.real_n[0] == 3 && stats.real_size[0] == 19);
    CHECK(stats.real_size[1] == 71);
    CHECK(stats.real_size[2] == 93 && stats.real_alignment[2] == 32);
    CHECK(stats.real_size[3] == 127 && stats.real_alignment[3] == 64);
}
void AllExternal() {
    ScreenJpegAllocationScope scope;
    Owned a(jpeg_calloc(3, 19)), b(jpeg_calloc_inner(71));
    Owned c(jpeg_calloc_align(93, 32)), d(jpeg_calloc_align_inner(127, 64));
    CheckExternal(a.get(), 57);
    CheckExternal(b.get(), 71);
    CheckExternal(c.get(), 93, 32);
    CheckExternal(d.get(), 127, 64);
    const auto stats = GetHeapStats();
    CHECK(stats.external_calls == 4 && stats.internal_calls == 0);
    for (auto count : stats.real_calls) CHECK(count == 0);
}
void ScopeRestores() {
    {
        ScreenJpegAllocationScope outer;
        { ScreenJpegAllocationScope inner; Owned p(jpeg_calloc_inner(23)); CheckExternal(p.get(), 23); }
        Owned p(jpeg_calloc_inner(29)); CheckExternal(p.get(), 29);
    }
    Owned p(jpeg_calloc_inner(31));
    CHECK(AllocationCaps(p.get()) == kInternal);
}
void UnwindRestores() {
    try {
        ScreenJpegAllocationScope scope;
        Owned p(jpeg_calloc_inner(27)); CheckExternal(p.get(), 27);
        throw std::bad_alloc();
    } catch (const std::bad_alloc&) {}
    CHECK(GetHeapStats().live == 0);
    Owned p(jpeg_calloc_inner(31));
    CHECK(AllocationCaps(p.get()) == kInternal);
}
void OomNeverFallsBack() {
    RejectExternal(true);
    {
        ScreenJpegAllocationScope scope;
        CHECK(jpeg_calloc(3, 19) == nullptr);
        CHECK(jpeg_calloc_inner(71) == nullptr);
        CHECK(jpeg_calloc_align(93, 32) == nullptr);
        CHECK(jpeg_calloc_align_inner(127, 64) == nullptr);
    }
    auto stats = GetHeapStats();
    CHECK(stats.external_calls == 4 && stats.internal_calls == 0 && stats.live == 0);
    for (auto count : stats.real_calls) CHECK(count == 0);
    // The same heap still has internal capacity: the original non-screen
    // policy retains its fallback rather than globally changing allocations.
    Owned a(jpeg_calloc(3, 19)), b(jpeg_calloc_align(93, 32));
    CHECK(AllocationCaps(a.get()) == kInternal && AllocationCaps(b.get()) == kInternal);
}
void EachFailureReleasesAndRecovers() {
    for (size_t failure = 1; failure <= 4; ++failure) {
        ResetHeap();
        FailExternalAt(failure);
        {
            ScreenJpegAllocationScope scope;
            std::array<Owned, 4> work{
                Owned(jpeg_calloc(3, 19)), Owned(jpeg_calloc_inner(71)),
                Owned(jpeg_calloc_align(93, 32)), Owned(jpeg_calloc_align_inner(127, 64))};
            for (size_t i = 0; i < work.size(); ++i) CHECK(static_cast<bool>(work[i]) == (i + 1 != failure));
        }
        CHECK(GetHeapStats().live == 0 && GetHeapStats().internal_calls == 0);
        ResetHeap();
        AllExternal();
        CHECK(GetHeapStats().live == 0);
    }
}
void ZeroOverflowAlignment() {
    ScreenJpegAllocationScope scope;
    CHECK(jpeg_calloc(0, 19) == nullptr);
    CHECK(jpeg_calloc(19, 0) == nullptr);
    CHECK(jpeg_calloc(std::numeric_limits<size_t>::max() / 2 + 1, 2) == nullptr);
    CHECK(jpeg_calloc_inner(0) == nullptr);
    for (int alignment : {0, 3, -1}) {
        CHECK(jpeg_calloc_align(19, alignment) == nullptr);
        CHECK(jpeg_calloc_align_inner(19, alignment) == nullptr);
    }
    for (int alignment : {1, 2, 4, 16, 64}) {
        Owned p(jpeg_calloc_align_inner(67, alignment));
        CheckExternal(p.get(), 67, alignment);
    }
    CHECK(GetHeapStats().internal_calls == 0);
}
void ScopeOutsideInvalidArgsPassThrough() {
    CHECK(jpeg_calloc(std::numeric_limits<size_t>::max(), 2) == nullptr);
    CHECK(jpeg_calloc_inner(0) == nullptr);
    CHECK(jpeg_calloc_align(5, -1) == nullptr);
    CHECK(jpeg_calloc_align_inner(7, 3) == nullptr);
    const auto stats = GetHeapStats();
    for (auto count : stats.real_calls) CHECK(count == 1);
    CHECK(stats.real_n[0] == std::numeric_limits<size_t>::max() && stats.real_size[0] == 2);
    CHECK(stats.real_size[1] == 0);
    CHECK(stats.real_alignment[2] == -1 && stats.real_size[2] == 5);
    CHECK(stats.real_alignment[3] == 3 && stats.real_size[3] == 7);
}
void FreeAfterScope() {
    void* aligned = nullptr;
    void* ordinary = nullptr;
    {
        ScreenJpegAllocationScope scope;
        aligned = jpeg_calloc_align_inner(96, 32);
        ordinary = jpeg_calloc_inner(73);
    }
    CHECK(GetHeapStats().live == 2);
    jpeg_free_align(aligned);
    jpeg_free(ordinary);
    CHECK(GetHeapStats().live == 0);
}
void ScopeDoesNotLeakToAnotherTask() {
    std::latch screen_entered(1), outside_done(1);
    std::atomic<bool> screen_ok{false}, outside_ok{false};
    std::thread screen([&] {
        ScreenJpegAllocationScope scope;
        Owned p(jpeg_calloc_inner(83));
        screen_ok = p && AllocationCaps(p.get()) == kExternal;
        screen_entered.count_down();
        outside_done.wait();
    });
    std::thread camera([&] {
        screen_entered.wait();
        Owned p(jpeg_calloc_inner(89));
        outside_ok = p && AllocationCaps(p.get()) == kInternal;
        outside_done.count_down();
    });
    screen.join(); camera.join();
    CHECK(screen_ok && outside_ok);
    CHECK(GetHeapStats().live == 0);
    Owned p(jpeg_calloc_inner(97));
    CHECK(AllocationCaps(p.get()) == kInternal);
}
}

int main() {
    struct Case { const char* name; void (*run)(); };
    const Case cases[] = {
        {"scope outside keeps all four original arguments and allocation policies", AllPassthrough},
        {"screen routes all four allocator sites to zeroed aligned PSRAM", AllExternal},
        {"nested scope restores previous policy", ScopeRestores},
        {"exception unwinding releases ownership and restores policy", UnwindRestores},
        {"screen OOM never falls back while outside fallback remains available", OomNeverFallsBack},
        {"each allocation failure frees owned buffers and a later call recovers", EachFailureReleasesAndRecovers},
        {"zero overflow invalid alignment and valid small alignment preserve heap contract", ZeroOverflowAlignment},
        {"outside invalid arguments still reach original functions unchanged", ScopeOutsideInvalidArgsPassThrough},
        {"original free functions release screen allocation after scope exits", FreeAfterScope},
        {"concurrent non-screen task never inherits screen allocation policy", ScopeDoesNotLeakToAnotherTask}
    };
    size_t failures = 0;
    for (const auto& test : cases) {
        try {
            ResetHeap(); test.run(); CHECK(GetHeapStats().live == 0);
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << std::size(cases) << " tests, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
