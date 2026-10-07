#include "camera-teardown-diagnostics.h"

#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define RODAK_CAMERA_TEARDOWN_DATA_ATTR DRAM_ATTR
#define RODAK_CAMERA_TEARDOWN_CODE_ATTR IRAM_ATTR
#else
#define RODAK_CAMERA_TEARDOWN_DATA_ATTR
#define RODAK_CAMERA_TEARDOWN_CODE_ATTR
#endif

static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr),
              "Camera teardown diagnostics require lock-free 32-bit atomics");
static_assert(sizeof(RodakCameraTeardownRecord) == 16);
static_assert(alignof(RodakCameraTeardownRecord) >= 4);
static_assert(offsetof(RodakCameraTeardownRecord, phase) == 0);
static_assert(offsetof(RodakCameraTeardownRecord, core) == 4);
static_assert(offsetof(RodakCameraTeardownRecord, status) == 8);
static_assert(offsetof(RodakCameraTeardownRecord, commit_seq) == 12);
static_assert(alignof(RodakCameraTeardownDiagnostics) >= 4);
static_assert(offsetof(RodakCameraTeardownDiagnostics, magic) == 0);
static_assert(offsetof(RodakCameraTeardownDiagnostics, version) == 4);
static_assert(offsetof(RodakCameraTeardownDiagnostics, capacity) == 8);
static_assert(offsetof(RodakCameraTeardownDiagnostics, next_index) == 12);
static_assert(offsetof(RodakCameraTeardownDiagnostics, contention_drop_seen) == 16);
static_assert(offsetof(RodakCameraTeardownDiagnostics, full_drop_seen) == 20);
static_assert(offsetof(RodakCameraTeardownDiagnostics, records) == 24);
static_assert(sizeof(RodakCameraTeardownDiagnostics) == 536);

extern "C" {
RODAK_CAMERA_TEARDOWN_DATA_ATTR RodakCameraTeardownDiagnostics
    rodak_camera_teardown_diagnostics = {
        RODAK_CAMERA_TEARDOWN_MAGIC,
        RODAK_CAMERA_TEARDOWN_VERSION,
        RODAK_CAMERA_TEARDOWN_CAPACITY,
        0,
        0,
        0,
        {}
    };
}

extern "C" RODAK_CAMERA_TEARDOWN_CODE_ATTR uint32_t
rodak_camera_teardown_record(uint32_t phase, uint32_t core, int32_t status) {
    auto& diagnostics = rodak_camera_teardown_diagnostics;
    uint32_t index = __atomic_load_n(&diagnostics.next_index, __ATOMIC_RELAXED);
    if (index >= RODAK_CAMERA_TEARDOWN_CAPACITY) {
        __atomic_store_n(&diagnostics.full_drop_seen, UINT32_C(1), __ATOMIC_RELEASE);
        return 0;
    }
    if (!__atomic_compare_exchange_n(&diagnostics.next_index, &index, index + 1,
                                     false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        __atomic_store_n(&diagnostics.contention_drop_seen, UINT32_C(1), __ATOMIC_RELEASE);
        return 0;
    }

    auto& record = diagnostics.records[index];
    record.phase = phase;
    record.core = core;
    record.status = status;
    // Once published this payload is immutable. A deleted writer can leave a
    // hole, but cannot strand a global lock and block the other core's records.
    __atomic_store_n(&record.commit_seq, index + 1, __ATOMIC_RELEASE);
    return index + 1;
}

extern "C" uint32_t
rodak_camera_teardown_snapshot(RodakCameraTeardownSnapshot* snapshot) {
    if (snapshot == nullptr) {
        return RODAK_CAMERA_TEARDOWN_SNAPSHOT_INVALID;
    }

    const auto& diagnostics = rodak_camera_teardown_diagnostics;
    const uint32_t begin = __atomic_load_n(&diagnostics.next_index, __ATOMIC_ACQUIRE);
    const uint32_t contention_begin =
        __atomic_load_n(&diagnostics.contention_drop_seen, __ATOMIC_ACQUIRE);
    const uint32_t full_begin =
        __atomic_load_n(&diagnostics.full_drop_seen, __ATOMIC_ACQUIRE);
    uint32_t flags = begin > RODAK_CAMERA_TEARDOWN_CAPACITY
        ? RODAK_CAMERA_TEARDOWN_SNAPSHOT_INVALID : 0;
    snapshot->claimed_begin = begin;
    snapshot->committed_records = 0;
    snapshot->pending_records = 0;

    for (uint32_t index = 0; index < RODAK_CAMERA_TEARDOWN_CAPACITY; ++index) {
        auto& copied = snapshot->records[index];
        copied = {};
        if (index >= begin) {
            continue;
        }
        const auto& record = diagnostics.records[index];
        const uint32_t committed = __atomic_load_n(&record.commit_seq, __ATOMIC_ACQUIRE);
        if (committed == index + 1) {
            copied.phase = record.phase;
            copied.core = record.core;
            copied.status = record.status;
            copied.commit_seq = committed;
            ++snapshot->committed_records;
        } else {
            ++snapshot->pending_records;
            flags |= RODAK_CAMERA_TEARDOWN_SNAPSHOT_PENDING;
            if (committed != 0) {
                flags |= RODAK_CAMERA_TEARDOWN_SNAPSHOT_INVALID;
            }
        }
    }

    snapshot->claimed_end = __atomic_load_n(&diagnostics.next_index, __ATOMIC_ACQUIRE);
    snapshot->contention_drop_seen =
        __atomic_load_n(&diagnostics.contention_drop_seen, __ATOMIC_ACQUIRE);
    snapshot->full_drop_seen = __atomic_load_n(&diagnostics.full_drop_seen, __ATOMIC_ACQUIRE);
    if (snapshot->claimed_end != begin || snapshot->contention_drop_seen != contention_begin ||
        snapshot->full_drop_seen != full_begin) {
        flags |= RODAK_CAMERA_TEARDOWN_SNAPSHOT_CHANGED;
    }
    if (snapshot->contention_drop_seen != 0 || snapshot->full_drop_seen != 0) {
        flags |= RODAK_CAMERA_TEARDOWN_SNAPSHOT_DROPPED;
    }
    if (snapshot->claimed_end > RODAK_CAMERA_TEARDOWN_CAPACITY) {
        flags |= RODAK_CAMERA_TEARDOWN_SNAPSHOT_INVALID;
    }
    return flags;
}
