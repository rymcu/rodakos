#include "camera-teardown-diagnostics.h"
#include "test_support.h"

#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>

extern "C" int rodak_test_camera_teardown_c_api(void);

namespace {

void CheckEmptyRecord(const RodakCameraTeardownRecord& record) {
    CHECK(record.phase == 0);
    CHECK(record.core == 0);
    CHECK(record.status == 0);
    CHECK(record.commit_seq == 0);
}

void Empty() {
    CHECK(rodak_camera_teardown_diagnostics.magic == RODAK_CAMERA_TEARDOWN_MAGIC);
    CHECK(rodak_camera_teardown_diagnostics.version == RODAK_CAMERA_TEARDOWN_VERSION);
    CHECK(rodak_camera_teardown_diagnostics.capacity == RODAK_CAMERA_TEARDOWN_CAPACITY);
    RodakCameraTeardownSnapshot snapshot;
    std::memset(&snapshot, 0xa5, sizeof(snapshot));
    CHECK(rodak_camera_teardown_snapshot(&snapshot) == 0);
    CHECK(snapshot.claimed_begin == 0);
    CHECK(snapshot.claimed_end == 0);
    CHECK(snapshot.committed_records == 0);
    CHECK(snapshot.pending_records == 0);
    CHECK(snapshot.contention_drop_seen == 0);
    CHECK(snapshot.full_drop_seen == 0);
    for (const auto& record : snapshot.records) {
        CheckEmptyRecord(record);
    }
    CHECK(rodak_camera_teardown_snapshot(nullptr) == RODAK_CAMERA_TEARDOWN_SNAPSHOT_INVALID);
}

void Sequence() {
    constexpr uint32_t phases[] = {
        RODAK_CAMERA_TEARDOWN_IOCTL_ENTER,
        RODAK_CAMERA_TEARDOWN_SENSOR_ENTER,
        RODAK_CAMERA_TEARDOWN_SENSOR_RETURNED,
        RODAK_CAMERA_TEARDOWN_STOP_ENTER,
        RODAK_CAMERA_TEARDOWN_STOP_RETURNED,
        RODAK_CAMERA_TEARDOWN_DISABLE_ENTER,
        RODAK_CAMERA_TEARDOWN_DISABLE_RETURNED,
        RODAK_CAMERA_TEARDOWN_DEL_ENTER,
        RODAK_CAMERA_TEARDOWN_DVP_TASK_DELETE_ENTER,
        RODAK_CAMERA_TEARDOWN_DVP_TASK_DELETE_RETURNED,
        RODAK_CAMERA_TEARDOWN_DVP_GPIO_DISABLE_ENTER,
        RODAK_CAMERA_TEARDOWN_DVP_GPIO_DISABLE_RETURNED,
        RODAK_CAMERA_TEARDOWN_DVP_CAPTURE_STOP_ENTER,
        RODAK_CAMERA_TEARDOWN_DVP_CAPTURE_STOP_RETURNED,
        RODAK_CAMERA_TEARDOWN_DVP_GPIO_REMOVE_ENTER,
        RODAK_CAMERA_TEARDOWN_DVP_GPIO_REMOVE_RETURNED,
        RODAK_CAMERA_TEARDOWN_DVP_GDMA_DISCONNECT_ENTER,
        RODAK_CAMERA_TEARDOWN_DVP_GDMA_DISCONNECT_RETURNED,
        RODAK_CAMERA_TEARDOWN_DVP_GDMA_DELETE_ENTER,
        RODAK_CAMERA_TEARDOWN_DVP_GDMA_DELETE_RETURNED,
        RODAK_CAMERA_TEARDOWN_DEL_RETURNED,
        RODAK_CAMERA_TEARDOWN_IOCTL_RETURNED,
        RODAK_CAMERA_TEARDOWN_BEFORE_LOG,
        RODAK_CAMERA_TEARDOWN_AFTER_LOG
    };
    constexpr int32_t statuses[] = {
        0, -1, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()
    };
    for (uint32_t index = 0; index < 24; ++index) {
        CHECK(rodak_camera_teardown_record(phases[index], index % 2, statuses[index % 4]) ==
              index + 1);
    }
    RodakCameraTeardownSnapshot snapshot;
    CHECK(rodak_camera_teardown_snapshot(&snapshot) == 0);
    CHECK(snapshot.claimed_begin == 24);
    CHECK(snapshot.claimed_end == 24);
    CHECK(snapshot.committed_records == 24);
    for (uint32_t index = 0; index < 24; ++index) {
        CHECK(snapshot.records[index].phase == phases[index]);
        CHECK(snapshot.records[index].core == index % 2);
        CHECK(snapshot.records[index].status == statuses[index % 4]);
        CHECK(snapshot.records[index].commit_seq == index + 1);
    }
    for (uint32_t index = 24; index < RODAK_CAMERA_TEARDOWN_CAPACITY; ++index) {
        CheckEmptyRecord(snapshot.records[index]);
    }
}

void Capacity() {
    for (uint32_t index = 0; index < RODAK_CAMERA_TEARDOWN_CAPACITY; ++index) {
        CHECK(rodak_camera_teardown_record(index + 1, index % 2, -static_cast<int32_t>(index)) ==
              index + 1);
    }
    RodakCameraTeardownSnapshot before;
    CHECK(rodak_camera_teardown_snapshot(&before) == 0);
    for (uint32_t attempt = 0; attempt < 100; ++attempt) {
        CHECK(rodak_camera_teardown_record(999, 99, -999) == 0);
    }
    RodakCameraTeardownSnapshot after;
    CHECK(rodak_camera_teardown_snapshot(&after) == RODAK_CAMERA_TEARDOWN_SNAPSHOT_DROPPED);
    CHECK(after.claimed_begin == RODAK_CAMERA_TEARDOWN_CAPACITY);
    CHECK(after.claimed_end == RODAK_CAMERA_TEARDOWN_CAPACITY);
    CHECK(after.committed_records == RODAK_CAMERA_TEARDOWN_CAPACITY);
    CHECK(after.pending_records == 0);
    CHECK(after.contention_drop_seen == 0);
    CHECK(after.full_drop_seen == 1);
    CHECK(std::memcmp(before.records, after.records, sizeof(before.records)) == 0);
}

void Pending() {
    // Model an abandoned reservation with partially written, unpublished data.
    // No reset or slot reuse occurs, and the real recorder claims slot 2 next.
    auto& diagnostics = rodak_camera_teardown_diagnostics;
    __atomic_store_n(&diagnostics.next_index, UINT32_C(1), __ATOMIC_RELAXED);
    diagnostics.records[0].phase = 999;
    diagnostics.records[0].core = 999;
    diagnostics.records[0].status = -999;
    CHECK(rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_AFTER_LOG, 1, -7) == 2);
    RodakCameraTeardownSnapshot snapshot;
    CHECK(rodak_camera_teardown_snapshot(&snapshot) == RODAK_CAMERA_TEARDOWN_SNAPSHOT_PENDING);
    CHECK(snapshot.claimed_begin == 2);
    CHECK(snapshot.claimed_end == 2);
    CHECK(snapshot.committed_records == 1);
    CHECK(snapshot.pending_records == 1);
    CheckEmptyRecord(snapshot.records[0]);
    CHECK(snapshot.records[1].phase == RODAK_CAMERA_TEARDOWN_AFTER_LOG);
    CHECK(snapshot.records[1].status == -7);
    CHECK(snapshot.records[1].commit_seq == 2);
}

void InvalidCount() {
    __atomic_store_n(&rodak_camera_teardown_diagnostics.next_index,
                     std::numeric_limits<uint32_t>::max(), __ATOMIC_RELAXED);
    RodakCameraTeardownSnapshot snapshot;
    CHECK(rodak_camera_teardown_snapshot(&snapshot) ==
          (RODAK_CAMERA_TEARDOWN_SNAPSHOT_INVALID | RODAK_CAMERA_TEARDOWN_SNAPSHOT_PENDING));
    CHECK(snapshot.pending_records == RODAK_CAMERA_TEARDOWN_CAPACITY);
    CHECK(snapshot.committed_records == 0);
    for (const auto& record : snapshot.records) {
        CheckEmptyRecord(record);
    }
    CHECK(rodak_camera_teardown_record(1, 0, 0) == 0);
}

void InvalidCommit() {
    __atomic_store_n(&rodak_camera_teardown_diagnostics.next_index, UINT32_C(1), __ATOMIC_RELAXED);
    __atomic_store_n(&rodak_camera_teardown_diagnostics.records[0].commit_seq,
                     UINT32_C(2), __ATOMIC_RELEASE);
    RodakCameraTeardownSnapshot snapshot;
    CHECK(rodak_camera_teardown_snapshot(&snapshot) ==
          (RODAK_CAMERA_TEARDOWN_SNAPSHOT_INVALID | RODAK_CAMERA_TEARDOWN_SNAPSHOT_PENDING));
    CHECK(snapshot.pending_records == 1);
    CheckEmptyRecord(snapshot.records[0]);
}

void CheckConcurrentSnapshot(const RodakCameraTeardownSnapshot& snapshot, uint32_t flags) {
    CHECK((flags & RODAK_CAMERA_TEARDOWN_SNAPSHOT_INVALID) == 0);
    CHECK(snapshot.claimed_begin <= RODAK_CAMERA_TEARDOWN_CAPACITY);
    CHECK(snapshot.claimed_end <= RODAK_CAMERA_TEARDOWN_CAPACITY);
    CHECK(snapshot.claimed_end >= snapshot.claimed_begin);
    CHECK(snapshot.committed_records + snapshot.pending_records == snapshot.claimed_begin);
    uint32_t committed = 0;
    for (uint32_t index = 0; index < RODAK_CAMERA_TEARDOWN_CAPACITY; ++index) {
        const auto& record = snapshot.records[index];
        if (record.commit_seq == 0) {
            CheckEmptyRecord(record);
            continue;
        }
        ++committed;
        CHECK(record.commit_seq == index + 1);
        CHECK(index < snapshot.claimed_begin);
        CHECK(record.core < 8);
        CHECK(record.phase >= 1000 + record.core * 64);
        CHECK(record.phase < 1000 + record.core * 64 + 64);
        CHECK(record.status == -static_cast<int32_t>(record.phase * 17));
    }
    CHECK(committed == snapshot.committed_records);
}

void Concurrent() {
    std::atomic<bool> start{false};
    std::atomic<uint32_t> finished{0};
    std::atomic<uint32_t> successful{0};
    std::array<std::atomic<uint32_t>, RODAK_CAMERA_TEARDOWN_CAPACITY> owners{};
    std::array<std::thread, 8> writers;
    for (uint32_t core = 0; core < writers.size(); ++core) {
        writers[core] = std::thread([&, core] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (uint32_t attempt = 0; attempt < 64; ++attempt) {
                const uint32_t phase = 1000 + core * 64 + attempt;
                const uint32_t sequence =
                    rodak_camera_teardown_record(phase, core, -static_cast<int32_t>(phase * 17));
                if (sequence != 0) {
                    owners[sequence - 1].fetch_add(1, std::memory_order_relaxed);
                    successful.fetch_add(1, std::memory_order_relaxed);
                }
                std::this_thread::yield();
            }
            finished.fetch_add(1, std::memory_order_release);
        });
    }
    start.store(true, std::memory_order_release);
    bool invalid = false;
    while (finished.load(std::memory_order_acquire) != writers.size()) {
        RodakCameraTeardownSnapshot snapshot;
        const uint32_t flags = rodak_camera_teardown_snapshot(&snapshot);
        try {
            CheckConcurrentSnapshot(snapshot, flags);
        } catch (...) {
            invalid = true;
        }
        std::this_thread::yield();
    }
    for (auto& writer : writers) {
        writer.join();
    }
    CHECK(!invalid);
    RodakCameraTeardownSnapshot snapshot;
    const uint32_t flags = rodak_camera_teardown_snapshot(&snapshot);
    CheckConcurrentSnapshot(snapshot, flags);
    CHECK((flags & RODAK_CAMERA_TEARDOWN_SNAPSHOT_PENDING) == 0);
    CHECK((flags & RODAK_CAMERA_TEARDOWN_SNAPSHOT_CHANGED) == 0);
    CHECK((flags & RODAK_CAMERA_TEARDOWN_SNAPSHOT_DROPPED) != 0);
    CHECK(snapshot.committed_records == successful.load());
    CHECK(snapshot.committed_records > 0);
    for (uint32_t index = 0; index < snapshot.committed_records; ++index) {
        CHECK(owners[index].load() == 1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        const std::string test = argv[1];
        if (test == "empty") Empty();
        else if (test == "c_api") CHECK(rodak_test_camera_teardown_c_api() == 0);
        else if (test == "sequence") Sequence();
        else if (test == "capacity") Capacity();
        else if (test == "pending") Pending();
        else if (test == "invalid_count") InvalidCount();
        else if (test == "invalid_commit") InvalidCommit();
        else if (test == "concurrent") Concurrent();
        else throw std::runtime_error("unknown test: " + test);
        std::cout << "[PASS] " << test << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
