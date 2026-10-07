#include "test_framework.h"
#include "host_runtime.h"
#include "host_heap.h"
#include "esp_heap_caps.h"
#include "phone_os/display_service.h"
#include "phone_os/task-retirement.h"
#include "task_retirement_host.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

namespace {
namespace host = rodakos_test::display_service_host;
using Service = rodakos::DisplayService;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct ResetHost { ResetHost() { host::Reset(); } };
struct Fixture {
    ResetHost reset;
    lv_display_t display;
    std::array<uint8_t, host::kFrameBytes> pixels{};
    Service service{&display};
    ~Fixture() {
        host::ClearFailures();
        service.StopJpegStream();
        host::JoinTasks();
    }
    void Start() { RODAK_CHECK(service.StartCapture()); }
    void Publish(uint16_t rgb565 = 0xf800) {
        for (size_t i = 0; i < pixels.size(); i += 2) {
            pixels[i] = static_cast<uint8_t>(rgb565);
            pixels[i + 1] = static_cast<uint8_t>(rgb565 >> 8);
        }
        host::Flush(display, pixels.data(), {0, 0, 319, 239});
    }
    void PublishMarkers() {
        pixels.fill(0);
        const auto set_pixel = [&](size_t index, uint16_t rgb565) {
            pixels[index * 2] = static_cast<uint8_t>(rgb565);
            pixels[index * 2 + 1] = static_cast<uint8_t>(rgb565 >> 8);
        };
        set_pixel(0, 0xf800);
        set_pixel((320 * 240) / 2, 0x07e0);
        set_pixel(320 * 240 - 1, 0x001f);
        host::Flush(display, pixels.data(), {0, 0, 319, 239});
    }
};

template <typename F> bool Wait(F&& predicate, std::chrono::milliseconds timeout = 2500ms) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return predicate();
}
void CheckReleased() {
    const auto state = host::Snapshot();
    RODAK_CHECK_EQ(state.buffers, 0u);
    RODAK_CHECK_EQ(state.bytes, 0u);
    RODAK_CHECK_EQ(state.encoders, 0u);
    RODAK_CHECK_EQ(state.opens, state.closes);
}
void CheckOutsidePolicy() {
    const auto before = host::Snapshot().original_allocator_calls[1];
    auto* pointer = jpeg_calloc_inner(37);
    const auto caps = AllocationCaps(pointer);
    jpeg_free(pointer);
    RODAK_CHECK_EQ(caps, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    RODAK_CHECK_EQ(host::Snapshot().original_allocator_calls[1], before + 1);
}
void CheckCadence(const std::vector<int64_t>& times) {
    RODAK_CHECK(times.size() >= 3);
    for (size_t i = 1; i < times.size(); ++i) RODAK_CHECK(times[i] - times[i - 1] >= 190000);
}
void CheckCaptureFailureAt(size_t allocation) {
    Fixture fixture;
    host::FailNew(host::kFrameBytes, allocation);
    RODAK_CHECK_FALSE(fixture.service.StartCapture());
    RODAK_CHECK_EQ(host::Snapshot().new_failures, 1u);
    RODAK_CHECK_FALSE(fixture.service.IsRunning());
    rodakos::DisplayFrame frame;
    RODAK_CHECK_FALSE(fixture.service.GetLatestFrame(frame));
    fixture.service.StopCapture();
    fixture.Start();
    fixture.Publish();
    RODAK_CHECK(fixture.service.GetLatestFrame(frame));
    RODAK_CHECK_EQ(frame.rgb565.size(), host::kFrameBytes);
    RODAK_CHECK_EQ(frame.sequence, 1u);
}
}

RODAK_TEST("capture first mirror allocation failure leaves an unlocked restartable service") {
    CheckCaptureFailureAt(1);
}
RODAK_TEST("capture second publication allocation failure rolls back and can restart") {
    CheckCaptureFailureAt(2);
}
RODAK_TEST("initial refresh rejection rolls capture back and later starts cleanly") {
    Fixture fixture;
    host::AllowAsync(false);
    RODAK_CHECK_FALSE(fixture.service.StartCapture());
    RODAK_CHECK_FALSE(fixture.service.IsRunning());
    host::AllowAsync(true);
    fixture.Start();
    fixture.Publish();
    rodakos::DisplayFrame frame;
    RODAK_CHECK(fixture.service.GetLatestFrame(frame));
}
RODAK_TEST("concurrent capture start attaches and detaches once under the LVGL lock") {
    host::Reset();
    lv_display_t display;
    {
        Service service(&display);
        std::atomic<int> starts{0};
        std::thread first([&] { if (service.StartCapture()) ++starts; });
        std::thread second([&] { if (service.StartCapture()) ++starts; });
        first.join();
        second.join();
        const auto attached = host::Snapshot();
        RODAK_CHECK_EQ(starts.load(), 2);
        RODAK_CHECK_EQ(attached.event_adds, 1u);
        RODAK_CHECK_EQ(attached.event_removes, 0u);
        RODAK_CHECK_EQ(attached.event_ops_without_lvgl_lock, 0u);
    }
    const auto detached = host::Snapshot();
    RODAK_CHECK_EQ(detached.event_adds, 1u);
    RODAK_CHECK_EQ(detached.event_removes, 1u);
    RODAK_CHECK_EQ(detached.event_ops_without_lvgl_lock, 0u);
    RODAK_CHECK_EQ(display.callback, nullptr);
    RODAK_CHECK_EQ(host::PendingAsync(), 0u);
}
RODAK_TEST("frame copy allocation failure clears caller output and unlocks before retry") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    rodakos::DisplayFrame frame{1, 1, 2, {0xab}, 123, 99};
    host::FailNew(host::kFrameBytes);
    RODAK_CHECK_FALSE(fixture.service.GetLatestFrame(frame));
    RODAK_CHECK_EQ(host::Snapshot().new_failures, 1u);
    RODAK_CHECK(frame.rgb565.empty());
    RODAK_CHECK_EQ(frame.sequence, 0u);
    RODAK_CHECK_EQ(frame.width, 0);
    RODAK_CHECK(fixture.service.IsRunning());
    RODAK_CHECK(fixture.service.GetLatestFrame(frame));
    RODAK_CHECK_EQ(frame.sequence, 1u);
    RODAK_CHECK_EQ(frame.rgb565[1], 0xf8);
}
RODAK_TEST("CaptureJpeg avoids a frame-sized deep copy before encoding") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    host::FailNew(host::kFrameBytes);
    std::vector<uint8_t> jpeg{1, 2, 3};
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK_EQ(host::Snapshot().new_failures, 0u);
    RODAK_CHECK_EQ(jpeg.size(), host::kJpegBytes);
    RODAK_CHECK(fixture.service.IsRunning());
    host::ClearFailures();
}
RODAK_TEST("capture with no ready frame cannot return an old JPEG") {
    Fixture fixture;
    fixture.Start();
    std::vector<uint8_t> jpeg{1, 2, 3};
    RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK(jpeg.empty());
}
RODAK_TEST("RGB888 allocation failure returns false without opening an encoder and recovers") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    host::FailHeap(host::kRgbBytes);
    std::vector<uint8_t> jpeg{9};
    RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK(jpeg.empty());
    RODAK_CHECK_EQ(host::Snapshot().heap_failures, 2u);
    RODAK_CHECK_EQ(host::Snapshot().opens, 0u);
    CheckReleased();
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK_EQ(jpeg.size(), host::kJpegBytes);
    CheckReleased();
}
RODAK_TEST("100 KiB scratch allocation failure after encoder open closes encoder and RGB888") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    host::FailHeap(host::kJpegScratchBytes, 2, true);
    std::vector<uint8_t> jpeg{9};
    RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK(jpeg.empty());
    const auto state = host::Snapshot();
    RODAK_CHECK_EQ(state.heap_failures, 2u);
    RODAK_CHECK_EQ(state.opens, 1u);
    RODAK_CHECK_EQ(state.processes, 0u);
    CheckReleased();
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    CheckReleased();
}
RODAK_TEST("JPEG heap-caps peak uses one RGB888 snapshot and bounded scratch") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::vector<uint8_t> jpeg;
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    const auto state = host::Snapshot();
    RODAK_CHECK_EQ(state.last_output_capacity, host::kJpegScratchBytes);
    RODAK_CHECK_EQ(state.peak_bytes, host::kRgbBytes + host::kJpegScratchBytes + host::kCodecWorkspaceBytes);
    CheckReleased();
}
RODAK_TEST("reverse in-place expansion preserves first middle and last RGB888 pixels") {
    Fixture fixture;
    fixture.Start();
    fixture.PublishMarkers();
    std::vector<uint8_t> jpeg;
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    const auto state = host::Snapshot();
    RODAK_CHECK_EQ(state.first_rgb888, 0xff0000u);
    RODAK_CHECK_EQ(state.middle_rgb888, 0x00ff00u);
    RODAK_CHECK_EQ(state.last_rgb888, 0x0000ffu);
    CheckReleased();
}
RODAK_TEST("compact JPEG allocation failure clears old output and releases every resource") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::vector<uint8_t> jpeg{9};
    host::FailNew(host::kJpegBytes);
    RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK_EQ(host::Snapshot().new_failures, 1u);
    RODAK_CHECK(jpeg.empty());
    CheckReleased();
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK_EQ(jpeg.size(), host::kJpegBytes);
    RODAK_CHECK_EQ(jpeg.capacity(), host::kJpegBytes);
    RODAK_CHECK_EQ(jpeg.front(), 255);
    CheckReleased();
}
RODAK_TEST("codec open and process failures release resources and can retry") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::vector<uint8_t> jpeg{9};
    host::FailEncoderOpen(true);
    RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK(jpeg.empty());
    CheckReleased();
    host::FailEncoderOpen(false);
    host::FailEncoderProcess(true);
    RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
    CheckReleased();
    host::ClearFailures();
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    CheckReleased();
}
RODAK_TEST("real EncodeJpeg keeps all codec allocations and close inside screen scope") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::vector<uint8_t> jpeg;
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    auto state = host::Snapshot();
    RODAK_CHECK_EQ(state.codec_external_calls, 5u);
    RODAK_CHECK_EQ(state.codec_internal_calls, 0u);
    RODAK_CHECK_EQ(state.close_scope_checks, 1u);
    for (auto calls : state.original_allocator_calls) RODAK_CHECK_EQ(calls, 0u);
    CheckReleased();
    CheckOutsidePolicy();
}
RODAK_TEST("each codec allocation OOM releases partial state and restores outside policy") {
    for (size_t bytes : {host::kCodecContextBytes, size_t(128), size_t(1024), size_t(2048)}) {
        Fixture fixture;
        fixture.Start();
        fixture.Publish();
        host::FailHeap(bytes, 1);
        std::vector<uint8_t> jpeg{9};
        RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
        RODAK_CHECK(jpeg.empty());
        RODAK_CHECK_EQ(host::Snapshot().heap_failures, 1u);
        RODAK_CHECK_EQ(host::Snapshot().codec_internal_calls, 0u);
        CheckReleased();
        CheckOutsidePolicy();
        RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
        CheckReleased();
    }
}
RODAK_TEST("screen codec external exhaustion never consumes available internal heap") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    host::RejectCodecExternal(true);
    std::vector<uint8_t> jpeg;
    RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK_EQ(host::Snapshot().codec_internal_calls, 0u);
    CheckReleased();
    CheckOutsidePolicy();
    host::ClearFailures();
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    CheckReleased();
}
RODAK_TEST("process exception closes encoder before restoring scope and a later encode works") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    host::ThrowEncoderProcess(true);
    std::vector<uint8_t> jpeg{9};
    RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
    RODAK_CHECK(jpeg.empty());
    RODAK_CHECK_EQ(host::Snapshot().close_scope_checks, 1u);
    RODAK_CHECK_EQ(host::Snapshot().codec_internal_calls, 0u);
    for (auto calls : host::Snapshot().original_allocator_calls) RODAK_CHECK_EQ(calls, 0u);
    CheckReleased();
    CheckOutsidePolicy();
    host::ClearFailures();
    RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
    CheckReleased();
}
RODAK_TEST("scratch and compact failures restore codec allocation policy") {
    for (bool scratch : {true, false}) {
        Fixture fixture;
        fixture.Start();
        fixture.Publish();
        if (scratch) host::FailHeap(host::kJpegScratchBytes, 2, true);
        else host::FailNew(host::kJpegBytes);
        std::vector<uint8_t> jpeg;
        RODAK_CHECK_FALSE(fixture.service.CaptureJpeg(jpeg));
        RODAK_CHECK_EQ(host::Snapshot().close_scope_checks, 1u);
        RODAK_CHECK_EQ(host::Snapshot().codec_internal_calls, 0u);
        CheckReleased();
        CheckOutsidePolicy();
        RODAK_CHECK(fixture.service.CaptureJpeg(jpeg));
        CheckReleased();
    }
}
RODAK_TEST("worker callback runs after the codec scope has restored its task policy") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::atomic<bool> finished{false}, original_policy{false};
    RODAK_CHECK(fixture.service.StartJpegStream(5, [&](auto&&, auto, auto) {
        auto* pointer = jpeg_calloc_inner(37);
        original_policy = AllocationCaps(pointer) == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        jpeg_free(pointer);
        finished = true;
    }));
    RODAK_CHECK(Wait([&] { return finished.load(); }));
    fixture.service.StopJpegStream();
    RODAK_CHECK(original_policy.load());
    CheckReleased();
}
RODAK_TEST("stream callback storage and task startup failures leave capture restartable") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    Service::JpegFrameCallback callback = [](auto&&, auto, auto) {};
    host::FailNew(SIZE_MAX);
    RODAK_CHECK_FALSE(fixture.service.StartJpegStream(5, std::move(callback)));
    RODAK_CHECK_EQ(host::Snapshot().new_failures, 1u);
    RODAK_CHECK(fixture.service.IsRunning());
    host::AllowTaskCreation(false);
    RODAK_CHECK_FALSE(fixture.service.StartJpegStream(5, [](auto&&, auto, auto) {}));
    host::AllowTaskCreation(true);
    std::atomic<int> callbacks{0};
    RODAK_CHECK(fixture.service.StartJpegStream(5, [&](auto&&, auto, auto) { ++callbacks; }));
    RODAK_CHECK(Wait([&] { return callbacks.load() == 1; }));
    fixture.service.StopJpegStream();
    CheckReleased();
}
RODAK_TEST("task startup failure releases a reentrant callback owner outside the service mutex") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::atomic<bool> released{false};
    std::atomic<bool> observed_running{false};
    auto token = std::shared_ptr<int>(new int(1), [&](int* value) {
        delete value;
        observed_running = fixture.service.IsRunning();
        released = true;
    });
    Service::JpegFrameCallback callback = [token](auto&&, auto, auto) {};
    token.reset();
    host::AllowTaskCreation(false);
    const bool started = fixture.service.StartJpegStream(5, std::move(callback));
    host::AllowTaskCreation(true);
    RODAK_CHECK_FALSE(started);
    RODAK_CHECK(released.load());
    RODAK_CHECK(observed_running.load());
}
RODAK_TEST("worker does not allocate frame-sized deep copies") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::atomic<int> callbacks{0};
    host::FailNew(host::kFrameBytes, 1, 100, true);
    RODAK_CHECK(fixture.service.StartJpegStream(5, [&](auto&&, auto, auto) { ++callbacks; }));
    RODAK_CHECK(Wait([&] { return callbacks.load() == 1; }));
    RODAK_CHECK_EQ(host::Snapshot().new_failures, 0u);
    RODAK_CHECK_EQ(callbacks.load(), 1);
    host::ClearFailures();
    fixture.service.StopJpegStream();
    CheckReleased();
}
RODAK_TEST("worker codec failures are paced and recover using the newest published frame") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish(0xf800);
    host::FailEncoderProcess(true);
    std::atomic<int> callbacks{0};
    std::atomic<uint32_t> sequence{0};
    std::atomic<unsigned> marker{99};
    RODAK_CHECK(fixture.service.StartJpegStream(5, [&](auto&& jpeg, auto seq, auto) {
        marker.store(jpeg.front()); sequence.store(seq); ++callbacks;
    }));
    std::jthread producer([&](std::stop_token stop) {
        while (!stop.stop_requested()) { fixture.Publish(0xf800); std::this_thread::sleep_for(10ms); }
    });
    RODAK_CHECK(Wait([] { return host::Snapshot().processes >= 3; }));
    producer.request_stop();
    producer.join();
    RODAK_CHECK(host::Snapshot().processes <= 4);
    RODAK_CHECK_EQ(callbacks.load(), 0);
    CheckCadence(host::ProcessTimes());
    fixture.Publish(0x001f);
    rodakos::DisplayFrame latest;
    RODAK_CHECK(fixture.service.GetLatestFrame(latest));
    host::ClearFailures();
    RODAK_CHECK(Wait([&] { return callbacks.load() == 1; }));
    RODAK_CHECK_EQ(sequence.load(), latest.sequence);
    RODAK_CHECK_EQ(marker.load(), 0u);
    fixture.service.StopJpegStream();
    CheckReleased();
}
RODAK_TEST("worker compact allocation failure sends no old JPEG and later sends compact newest data") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish(0xf800);
    host::FailNew(host::kJpegBytes, 1, 100, true);
    std::atomic<int> callbacks{0};
    std::atomic<uint32_t> sequence{0};
    std::atomic<size_t> capacity{0};
    std::atomic<unsigned> marker{99};
    RODAK_CHECK(fixture.service.StartJpegStream(5, [&](auto&& jpeg, auto seq, auto) {
        marker.store(jpeg.front()); capacity.store(jpeg.capacity()); sequence.store(seq); ++callbacks;
    }));
    std::jthread producer([&](std::stop_token stop) {
        while (!stop.stop_requested()) { fixture.Publish(0xf800); std::this_thread::sleep_for(10ms); }
    });
    RODAK_CHECK(Wait([] { return host::Snapshot().new_failures >= 3; }));
    producer.request_stop();
    producer.join();
    RODAK_CHECK(host::Snapshot().new_failures <= 4);
    RODAK_CHECK_EQ(callbacks.load(), 0);
    CheckCadence(host::NewFailureTimes());
    fixture.Publish(0x001f);
    rodakos::DisplayFrame latest;
    RODAK_CHECK(fixture.service.GetLatestFrame(latest));
    host::ClearFailures();
    RODAK_CHECK(Wait([&] { return callbacks.load() == 1; }));
    RODAK_CHECK_EQ(sequence.load(), latest.sequence);
    RODAK_CHECK_EQ(marker.load(), 0u);
    RODAK_CHECK_EQ(capacity.load(), host::kJpegBytes);
    fixture.service.StopJpegStream();
    CheckReleased();
}
RODAK_TEST("worker drops JPEG output beyond 100 KiB and recovers on a newer frame") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish(0xf800);
    host::SetEncodedSize(host::kJpegScratchBytes + 1);
    std::atomic<int> callbacks{0};
    std::atomic<unsigned> marker{99};
    RODAK_CHECK(fixture.service.StartJpegStream(5, [&](auto&& jpeg, auto, auto) {
        marker.store(jpeg.front());
        ++callbacks;
    }));
    RODAK_CHECK(Wait([] { return host::Snapshot().processes >= 2; }));
    RODAK_CHECK_EQ(callbacks.load(), 0);
    host::SetEncodedSize(host::kJpegBytes);
    fixture.Publish(0x001f);
    RODAK_CHECK(Wait([&] { return callbacks.load() == 1; }));
    RODAK_CHECK_EQ(marker.load(), 0u);
    fixture.service.StopJpegStream();
    CheckReleased();
}
RODAK_TEST("throwing callback is not replayed and a later frame can still be delivered") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::atomic<int> callbacks{0};
    std::atomic<uint32_t> sequence{0};
    RODAK_CHECK(fixture.service.StartJpegStream(5, [&](auto&&, auto seq, auto) {
        sequence.store(seq);
        if (++callbacks == 1) throw std::bad_alloc();
    }));
    RODAK_CHECK(Wait([&] { return callbacks.load() == 1; }));
    std::this_thread::sleep_for(450ms);
    RODAK_CHECK_EQ(callbacks.load(), 1);
    fixture.Publish(0x001f);
    RODAK_CHECK(Wait([&] { return callbacks.load() == 2; }));
    RODAK_CHECK_EQ(sequence.load(), 2u);
    fixture.service.StopJpegStream();
    CheckReleased();
}
RODAK_TEST("callback can stop its own worker and releases stored callback ownership") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    auto token = std::make_shared<int>(1);
    const std::weak_ptr<int> weak = token;
    std::atomic<int> callbacks{0};
    Service::JpegFrameCallback callback = [&, token](auto&&, auto, auto) {
        fixture.service.StopJpegStream();
        ++callbacks;
    };
    token.reset();
    RODAK_CHECK(fixture.service.StartJpegStream(5, std::move(callback)));
    RODAK_CHECK(Wait([&] { return callbacks.load() == 1; }));
    host::JoinTasks();
    RODAK_CHECK(weak.expired());
    CheckReleased();
    RODAK_CHECK(fixture.service.StartJpegStream(5, [&](auto&&, auto, auto) { ++callbacks; }));
    RODAK_CHECK(Wait([&] { return callbacks.load() == 2; }));
    fixture.service.StopJpegStream();
}
RODAK_TEST("Stop waits only for its old generation when callback destruction starts a replacement") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false, release = false;
    std::atomic<bool> replacement_attempted{false};
    std::atomic<bool> replacement_started{false};
    std::atomic<bool> stop_returned{false};
    auto restart_token = std::shared_ptr<int>(new int(1), [&](int* value) {
        delete value;
        replacement_started = fixture.service.StartJpegStream(5, [](auto&&, auto, auto) {});
        replacement_attempted = true;
    });
    Service::JpegFrameCallback callback = [&, restart_token](auto&&, auto, auto) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return release; });
    };
    restart_token.reset();
    RODAK_CHECK(fixture.service.StartJpegStream(5, std::move(callback)));
    bool received = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        received = condition.wait_for(lock, 2s, [&] { return entered; });
    }
    std::thread stopper([&] {
        fixture.service.StopJpegStream();
        stop_returned = true;
    });
    std::this_thread::sleep_for(50ms);
    const bool returned_before_release = stop_returned.load();
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
        condition.notify_all();
    }
    const bool replacement_was_attempted = Wait([&] { return replacement_attempted.load(); });
    const bool old_stop_completed = Wait([&] { return stop_returned.load(); }, 500ms);
    fixture.service.StopJpegStream();
    stopper.join();
    RODAK_CHECK(received);
    RODAK_CHECK_FALSE(returned_before_release);
    RODAK_CHECK(replacement_was_attempted);
    RODAK_CHECK(replacement_started.load());
    RODAK_CHECK(old_stop_completed);
}
RODAK_TEST("retirement waits for late callback destruction before freeing display task") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    retirement_host::Gate destructor;
    auto token = std::shared_ptr<int>(new int(1), [&](int* value) {
        delete value;
        destructor.Enter();
    });
    Service::JpegFrameCallback callback = [&, token](auto&&, auto, auto) {
        fixture.service.StopJpegStream();
    };
    token.reset();
    RODAK_CHECK(fixture.service.StartJpegStream(5, std::move(callback)));
    const bool destructor_entered = destructor.Wait();
    std::atomic<bool> returned{false};
    std::thread stopping([&] { fixture.service.StopJpegStream(); returned = true; });
    std::this_thread::sleep_for(30ms);
    rodakos::PumpTaskRetirements();
    const bool returned_early = returned.load();
    const size_t deleted_early = retirement_host::Snapshot().task_deletes;
    destructor.Release();
    stopping.join();
    host::JoinTasks();
    RODAK_CHECK(destructor_entered);
    RODAK_CHECK_FALSE(returned_early);
    RODAK_CHECK_EQ(deleted_early, 0u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
}
RODAK_TEST("retirement display exits without allocating an IDF cleanup task") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    retirement_host::RejectCleanupTask(true);
    RODAK_CHECK(fixture.service.StartJpegStream(5, [](auto&&, auto, auto) {}));
    fixture.service.StopJpegStream();
    host::JoinTasks();
    const auto tasks = retirement_host::Snapshot();
    RODAK_CHECK_EQ(tasks.cleanup_create_attempts, 0u);
    RODAK_CHECK_EQ(tasks.task_deletes, 1u);
    RODAK_CHECK_EQ(tasks.live_tasks, 0u);
    RODAK_CHECK_EQ(tasks.live_task_buffers, 0u);
}
RODAK_TEST("retirement display concurrent Stop and pump reclaim exactly once") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    retirement_host::Gate destructor;
    auto token = std::shared_ptr<int>(new int(1), [&](int* value) {
        delete value;
        destructor.Enter();
    });
    Service::JpegFrameCallback callback = [&, token](auto&&, auto, auto) {
        fixture.service.StopJpegStream();
    };
    token.reset();
    RODAK_CHECK(fixture.service.StartJpegStream(5, std::move(callback)));
    const bool entered = destructor.Wait();
    std::atomic<size_t> joined{0};
    std::thread first([&] { fixture.service.StopJpegStream(); ++joined; });
    std::thread second([&] { fixture.service.StopJpegStream(); ++joined; });
    std::thread pump([&] {
        while (joined.load() != 2) {
            rodakos::PumpTaskRetirements();
            std::this_thread::sleep_for(1ms);
        }
    });
    destructor.Release();
    first.join();
    second.join();
    pump.join();
    host::JoinTasks();
    RODAK_CHECK(entered);
    RODAK_CHECK_EQ(joined.load(), 2u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
}
RODAK_TEST("external Stop waits for an admitted callback then prevents later callbacks") {
    Fixture fixture;
    fixture.Start();
    fixture.Publish();
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false, release = false;
    std::atomic<int> callbacks{0};
    RODAK_CHECK(fixture.service.StartJpegStream(5, [&](auto&&, auto, auto) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true; ++callbacks; condition.notify_all();
        condition.wait(lock, [&] { return release; });
    }));
    bool received = false;
    { std::unique_lock<std::mutex> lock(mutex); received = condition.wait_for(lock, 2s, [&] { return entered; }); }
    std::atomic<bool> stop_returned{false};
    std::thread stopper([&] { fixture.service.StopJpegStream(); stop_returned = true; });
    std::this_thread::sleep_for(50ms);
    const bool returned_early = stop_returned.load();
    { std::lock_guard<std::mutex> lock(mutex); release = true; condition.notify_all(); }
    stopper.join();
    RODAK_CHECK(received);
    RODAK_CHECK_FALSE(returned_early);
    RODAK_CHECK(stop_returned.load());
    fixture.Publish(0x001f);
    std::this_thread::sleep_for(230ms);
    RODAK_CHECK_EQ(callbacks.load(), 1);
    CheckReleased();
}
RODAK_TEST("stop during sustained allocation failures is bounded and destruction cancels callbacks") {
    host::Reset();
    lv_display_t display;
    {
        Service service(&display);
        RODAK_CHECK(service.StartCapture());
        std::array<uint8_t, host::kFrameBytes> pixels{};
        host::Flush(display, pixels.data(), {0, 0, 319, 239});
        host::FailHeap(host::kRgbBytes, 100);
        auto token = std::make_shared<int>(1);
        const std::weak_ptr<int> weak = token;
        Service::JpegFrameCallback callback = [token](auto&&, auto, auto) {};
        token.reset();
        RODAK_CHECK(service.StartJpegStream(1, std::move(callback)));
        RODAK_CHECK(Wait([] { return host::Snapshot().heap_failures >= 1; }));
        const auto started = Clock::now();
        service.StopJpegStream();
        const auto elapsed = Clock::now() - started;
        host::ClearFailures();
        host::JoinTasks();
        RODAK_CHECK(elapsed < 300ms);
        RODAK_CHECK(weak.expired());
        CheckReleased();
        RODAK_CHECK(host::PendingAsync() > 0);
    }
    RODAK_CHECK_EQ(host::PendingAsync(), 0u);
    RODAK_CHECK_EQ(display.callback, nullptr);
}
