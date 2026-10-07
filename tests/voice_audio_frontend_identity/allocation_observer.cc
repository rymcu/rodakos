#include "host_runtime.h"

#include <atomic>
#include <cstdio>

namespace {
std::atomic<void*> feed_buffer{nullptr};
std::atomic<bool> feed_buffer_released{false};
}

namespace rodakos_test::voice_frontend {
void ObserveAfeFeedBuffer(void* pointer) {
    if (!feed_buffer.load()) {
        std::fputs("CAPTURE_AFE_FEED_OBSERVED\n", stderr);
        std::fflush(stderr);
    }
    feed_buffer.store(pointer);
    feed_buffer_released.store(false);
}
void ObserveDelete(void* pointer) {
    if (pointer && feed_buffer.load() == pointer && !feed_buffer_released.exchange(true)) {
        std::fputs("CAPTURE_AFE_BUFFER_RELEASED\n", stderr);
        std::fflush(stderr);
    }
}
void ResetAllocationObserver() {
    feed_buffer.store(nullptr);
    feed_buffer_released.store(false);
}
void* LastAfeFeedBuffer() { return feed_buffer.load(); }
bool AfeFeedBufferReleased() { return feed_buffer_released.load(); }
}

// Observe the real std::vector allocation without replacing its allocator or
// turning task deletion into an exception that unwinds production locals.
extern "C" void __real__ZdlPv(void*);
extern "C" void __real__ZdlPvm(void*, size_t);
extern "C" void __wrap__ZdlPv(void* pointer) {
    rodakos_test::voice_frontend::ObserveDelete(pointer);
    __real__ZdlPv(pointer);
}
extern "C" void __wrap__ZdlPvm(void* pointer, size_t bytes) {
    rodakos_test::voice_frontend::ObserveDelete(pointer);
    __real__ZdlPvm(pointer, bytes);
}
