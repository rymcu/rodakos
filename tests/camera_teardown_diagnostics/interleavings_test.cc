#include "camera-teardown-diagnostics.h"
#include "test_support.h"

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

enum class Mode { kNone, kCollide, kSuspend, kChange };
std::atomic<Mode> mode{Mode::kNone};
std::atomic<uint32_t> cas_calls{0};
std::mutex gate;
std::condition_variable condition;
bool reserved = false;
bool resume = false;

void Collision() {
    mode.store(Mode::kCollide);
    CHECK(rodak_camera_teardown_record(1, 0, -1) == 0);
    CHECK(cas_calls.load() == 2);  // Original plus the injected competing writer; no retry.
    RodakCameraTeardownSnapshot snapshot;
    CHECK(rodak_camera_teardown_snapshot(&snapshot) == RODAK_CAMERA_TEARDOWN_SNAPSHOT_DROPPED);
    CHECK(snapshot.contention_drop_seen == 1);
    CHECK(snapshot.full_drop_seen == 0);
    CHECK(snapshot.committed_records == 1);
    CHECK(snapshot.records[0].phase == 7);
    CHECK(snapshot.records[0].core == 1);
    CHECK(snapshot.records[0].status == -7);
    CHECK(rodak_camera_teardown_record(2, 0, -2) == 2);
}

void Suspended() {
    mode.store(Mode::kSuspend);
    uint32_t first = 0;
    std::thread suspended([&] { first = rodak_camera_teardown_record(1, 0, -1); });
    {
        std::unique_lock<std::mutex> lock(gate);
        condition.wait(lock, [] { return reserved; });
    }
    const uint32_t second = rodak_camera_teardown_record(2, 1, -2);
    RodakCameraTeardownSnapshot partial;
    const uint32_t flags = rodak_camera_teardown_snapshot(&partial);
    {
        std::lock_guard<std::mutex> lock(gate);
        resume = true;
    }
    condition.notify_all();
    suspended.join();
    CHECK(second == 2);
    CHECK(flags == RODAK_CAMERA_TEARDOWN_SNAPSHOT_PENDING);
    CHECK(partial.claimed_begin == 2);
    CHECK(partial.claimed_end == 2);
    CHECK(partial.pending_records == 1);
    CHECK(partial.committed_records == 1);
    CHECK(partial.records[0].phase == 0);
    CHECK(partial.records[0].core == 0);
    CHECK(partial.records[0].status == 0);
    CHECK(partial.records[0].commit_seq == 0);
    CHECK(partial.records[1].phase == 2);
    CHECK(partial.records[1].core == 1);
    CHECK(partial.records[1].status == -2);
    CHECK(partial.records[1].commit_seq == 2);
    CHECK(first == 1);
    RodakCameraTeardownSnapshot final;
    CHECK(rodak_camera_teardown_snapshot(&final) == 0);
    CHECK(final.committed_records == 2);
    CHECK(final.records[0].phase == 1);
    CHECK(final.records[0].status == -1);
}

void Changed() {
    CHECK(rodak_camera_teardown_record(1, 0, -1) == 1);
    mode.store(Mode::kChange);
    RodakCameraTeardownSnapshot snapshot;
    CHECK(rodak_camera_teardown_snapshot(&snapshot) == RODAK_CAMERA_TEARDOWN_SNAPSHOT_CHANGED);
    CHECK(snapshot.claimed_begin == 1);
    CHECK(snapshot.claimed_end == 2);
    CHECK(snapshot.committed_records == 1);
    CHECK(snapshot.pending_records == 0);
    CHECK(snapshot.records[0].phase == 1);
    CHECK(snapshot.records[1].commit_seq == 0);
    CHECK(snapshot.records[1].phase == 0);
    CHECK(rodak_camera_teardown_snapshot(&snapshot) == 0);
    CHECK(snapshot.committed_records == 2);
    CHECK(snapshot.records[1].phase == 2);
}

}  // namespace

extern "C" bool rodak_test_compare_exchange(uint32_t* address, uint32_t* expected,
    uint32_t desired, bool weak, int success_order, int failure_order) {
    cas_calls.fetch_add(1);
    const Mode active = mode.exchange(Mode::kNone);
    if (active == Mode::kCollide) {
        // A real competing recorder wins after the outer writer's initial load.
        rodak_camera_teardown_record(7, 1, -7);
    }
    const bool result = __atomic_compare_exchange_n(address, expected, desired,
                                                    weak, success_order, failure_order);
    if (active == Mode::kSuspend && result) {
        std::unique_lock<std::mutex> lock(gate);
        reserved = true;
        condition.notify_all();
        condition.wait(lock, [] { return resume; });
    }
    return result;
}

extern "C" uint32_t rodak_test_load(const uint32_t* address, int order) {
    if (address == &rodak_camera_teardown_diagnostics.records[0].commit_seq &&
        mode.exchange(Mode::kNone) == Mode::kChange) {
        rodak_camera_teardown_record(2, 1, -2);
    }
    return __atomic_load_n(address, order);
}

int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        const std::string test = argv[1];
        if (test == "collision") Collision();
        else if (test == "suspended") Suspended();
        else if (test == "changed") Changed();
        else throw std::runtime_error("unknown test: " + test);
        std::cout << "[PASS] " << test << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
