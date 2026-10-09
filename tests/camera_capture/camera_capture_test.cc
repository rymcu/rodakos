#include "test_framework.h"
#include "host_runtime.h"
#include "../task_retirement/task_retirement_host.h"
#include "phone_os/camera_service.h"
#include "phone_os/task-retirement.h"
#include "rodakos_adapters/file_service.h"

#include <array>
#include <chrono>
#include <new>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <set>
#include <thread>

namespace {
using namespace rodakos;
using Bytes = std::vector<uint8_t>;

struct Fixture {
    std::string root;
    std::unique_ptr<FileService> files;
    std::unique_ptr<CameraService> camera;
    Fixture() {
        char name[] = "/tmp/rodakos-camera-capture-XXXXXX";
        const char* path = mkdtemp(name);
        RODAK_CHECK(path != nullptr);
        root = path;
        camera_host::Reset(root);
        files.reset(CreateFileService());
        camera = std::make_unique<CameraService>(files.get());
    }
    ~Fixture() {
        camera_host::ClearNewFailures();
        camera.reset();
        camera_host::JoinTasks();
        files.reset();
        camera_host::Reset("");
        std::filesystem::remove_all(root);
    }
    void Preview(CameraService* target = nullptr) {
        if (target == nullptr) target = camera.get();
        RODAK_CHECK(target->StartPreview(2, 2));
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            if (target->GetState().has_frame) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("fake camera did not deliver a preview frame");
    }
    std::string Save() {
        std::string path = "stale-output";
        RODAK_CHECK(camera->CapturePhoto(path));
        RODAK_CHECK_FALSE(path.empty());
        return path;
    }
    Bytes Read(const std::string& path) {
        std::ifstream input(root + path, std::ios::binary);
        RODAK_CHECK(input.good());
        return Bytes(std::istreambuf_iterator<char>(input), {});
    }
    size_t PhotoCount() const {
        const auto dir = std::filesystem::path(root) / "photos";
        if (!std::filesystem::exists(dir)) return 0;
        return static_cast<size_t>(std::distance(std::filesystem::directory_iterator(dir),
                                                std::filesystem::directory_iterator{}));
    }
};
}

RODAK_TEST("camera reports a saved path only after real exclusive file output succeeds") {
    Fixture f; f.Preview();
    const auto saved = f.Save();
    RODAK_CHECK_EQ(f.Read(saved), camera_host::EncodedBytes());
    const auto state = f.camera->GetState();
    RODAK_CHECK_EQ(state.last_saved_path, saved);
    RODAK_CHECK(state.last_error.empty());
    RODAK_CHECK(state.preview_running);
    RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
}

RODAK_TEST("missing camera frame or file service never publishes stale caller output") {
    Fixture f;
    std::string path = "/photos/old.jpg";
    RODAK_CHECK_FALSE(f.camera->CapturePhoto(path));
    RODAK_CHECK(path.empty());
    RODAK_CHECK(f.camera->GetState().last_saved_path.empty());
    CameraService no_files(nullptr);
    f.Preview(&no_files);
    path = "/photos/old.jpg";
    RODAK_CHECK_FALSE(no_files.CapturePhoto(path));
    RODAK_CHECK(path.empty());
    RODAK_CHECK_EQ(no_files.last_error(), "File service is not available");
    RODAK_CHECK_EQ(f.PhotoCount(), 0u);
}

RODAK_TEST("encoder and input allocation failures preserve prior success and allow retry") {
    Fixture f; f.Preview(); const auto saved = f.Save();
    std::atomic<bool>* failures[] = {&camera_host::fail_encoder_open, &camera_host::fail_encoder_process,
                                   &camera_host::empty_encoded, &camera_host::fail_allocation};
    for (auto* failure : failures) {
        *failure = true;
        std::string path = saved;
        RODAK_CHECK_FALSE(f.camera->CapturePhoto(path));
        RODAK_CHECK(path.empty());
        RODAK_CHECK_EQ(f.camera->GetState().last_saved_path, saved);
        RODAK_CHECK_FALSE(f.camera->last_error().empty());
        RODAK_CHECK_EQ(f.PhotoCount(), 1u);
        RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
        *failure = false;
    }
    RODAK_CHECK_NE(f.Save(), saved);
    RODAK_CHECK_EQ(f.Read(saved), camera_host::EncodedBytes());
}

RODAK_TEST("mount and directory creation failures leave no saved result and can retry") {
    Fixture f; f.Preview();
    camera_host::fail_mount = true;
    std::string path = "old";
    RODAK_CHECK_FALSE(f.camera->CapturePhoto(path));
    RODAK_CHECK(path.empty());
    RODAK_CHECK_EQ(f.camera->last_error(), "SD card is not available");
    camera_host::fail_mount = false;
    camera_host::fail_directory = true;
    RODAK_CHECK_FALSE(f.camera->CapturePhoto(path));
    RODAK_CHECK(path.empty());
    RODAK_CHECK_EQ(f.camera->last_error(), "Failed to create /photos on SD card");
    RODAK_CHECK(f.camera->GetState().last_saved_path.empty());
    RODAK_CHECK_EQ(f.PhotoCount(), 0u);
    RODAK_CHECK_FALSE(f.Save().empty());
}

RODAK_TEST("short write flush and close failures cannot report photos or damage a prior photo") {
    for (unsigned fault = 0; fault < 3; ++fault) {
        Fixture f; f.Preview(); const auto previous = f.Save();
        if (fault == 0) camera_host::fail_write = true;
        if (fault == 1) camera_host::fail_flush = true;
        if (fault == 2) camera_host::fail_close = true;
        std::string path = previous;
        RODAK_CHECK_FALSE(f.camera->CapturePhoto(path));
        RODAK_CHECK(path.empty());
        RODAK_CHECK_EQ(f.camera->GetState().last_saved_path, previous);
        RODAK_CHECK_EQ(f.camera->last_error(), "Failed to save photo");
        RODAK_CHECK_EQ(f.Read(previous), camera_host::EncodedBytes());
        RODAK_CHECK_EQ(f.PhotoCount(), 1u);
        const auto retried = f.Save();
        RODAK_CHECK_NE(retried, previous);
        RODAK_CHECK_EQ(f.Read(retried), camera_host::EncodedBytes());
        RODAK_CHECK(f.camera->last_error().empty());
    }
}

RODAK_TEST("a concurrent external create is preserved and the next capture chooses a new path") {
    Fixture f; f.Preview();
    camera_host::collide_on_create = true;
    std::string path = "stale-output";
    RODAK_CHECK_FALSE(f.camera->CapturePhoto(path));
    RODAK_CHECK(path.empty());
    RODAK_CHECK(f.camera->GetState().last_saved_path.empty());
    RODAK_CHECK_FALSE(camera_host::collision_path.empty());
    const auto collided = camera_host::collision_path.substr(f.root.size());
    const std::string expected = "existing-photo";
    RODAK_CHECK_EQ(f.Read(collided), Bytes(expected.begin(), expected.end()));
    const auto retry = f.Save();
    RODAK_CHECK_NE(retry, collided);
    RODAK_CHECK_EQ(f.PhotoCount(), 2u);
    RODAK_CHECK_EQ(f.Read(collided), Bytes(expected.begin(), expected.end()));
}

RODAK_TEST("in-flight storage never publishes a candidate path as an already saved photo") {
    Fixture f; f.Preview(); const auto previous = f.Save();
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    camera_host::write_hook = [&]() { entered.set_value(); gate.wait(); };
    std::string pending = "stale-output";
    auto save = std::async(std::launch::async, [&]() { return f.camera->CapturePhoto(pending); });
    entered.get_future().wait();
    const bool no_result = pending.empty();
    const auto snapshot = f.camera->GetState();
    release.set_value();
    const bool saved = save.get();
    camera_host::write_hook = {};
    RODAK_CHECK(no_result);
    RODAK_CHECK_EQ(snapshot.last_saved_path, previous);
    RODAK_CHECK(saved);
    RODAK_CHECK_EQ(f.camera->GetState().last_saved_path, pending);
    RODAK_CHECK_EQ(f.Read(pending), camera_host::EncodedBytes());
}

RODAK_TEST("concurrent captures with frozen time keep distinct complete photos") {
    Fixture f; f.Preview();
    const auto original = f.Save();
    std::vector<std::future<std::string>> captures;
    for (unsigned index = 0; index < 8; ++index) {
        captures.push_back(std::async(std::launch::async, [&]() {
            std::string path;
            if (!f.camera->CapturePhoto(path)) throw std::runtime_error("concurrent capture failed");
            return path;
        }));
    }
    std::set<std::string> paths{original};
    for (auto& result : captures) paths.insert(result.get());
    RODAK_CHECK_EQ(paths.size(), 9u);
    RODAK_CHECK_EQ(f.PhotoCount(), 9u);
    for (const auto& path : paths) RODAK_CHECK_EQ(f.Read(path), camera_host::EncodedBytes());
    RODAK_CHECK(paths.count(f.camera->GetState().last_saved_path) != 0);
    RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
}

RODAK_TEST("camera destruction drains an in-flight save before deleting service state") {
    Fixture f; f.Preview();
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    camera_host::write_hook = [&]() { entered.set_value(); gate.wait(); };
    auto* camera = f.camera.release();
    std::string saved_path;
    auto save = std::async(std::launch::async, [&]() { return camera->CapturePhoto(saved_path); });
    entered.get_future().wait();
    auto destroy = std::async(std::launch::async, [camera]() { delete camera; });
    const bool waited = destroy.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    release.set_value();
    const bool saved = save.get();
    destroy.get();
    camera_host::write_hook = {};
    camera_host::JoinTasks();
    RODAK_CHECK(waited);
    RODAK_CHECK(saved);
    RODAK_CHECK_EQ(f.Read(saved_path), camera_host::EncodedBytes());
    RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
}

RODAK_TEST("two camera services sharing storage serialize name selection and exclusive creation") {
    Fixture f; f.Preview();
    CameraService second(f.files.get()); f.Preview(&second);
    auto one = std::async(std::launch::async, [&]() { return f.Save(); });
    std::string second_path;
    RODAK_CHECK(second.CapturePhoto(second_path));
    const auto first_path = one.get();
    RODAK_CHECK_NE(first_path, second_path);
    RODAK_CHECK_EQ(f.Read(first_path), camera_host::EncodedBytes());
    RODAK_CHECK_EQ(f.Read(second_path), camera_host::EncodedBytes());
    RODAK_CHECK_EQ(f.PhotoCount(), 2u);
}

RODAK_TEST("destruction drains a JPEG callback capture without taking its save lock first") {
    Fixture f; f.Preview();
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    camera_host::write_hook = [&]() { entered.set_value(); gate.wait(); };
    auto* camera = f.camera.release();
    std::atomic<bool> saved{false};
    std::string path;
    RODAK_CHECK(camera->StartJpegStream(30, [&](Bytes&&, uint32_t, int64_t) {
        saved = camera->CapturePhoto(path);
    }));
    entered.get_future().wait();
    auto destroy = std::async(std::launch::async, [camera]() { delete camera; });
    const bool waited = destroy.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    release.set_value();
    destroy.get();
    camera_host::write_hook = {};
    camera_host::JoinTasks();
    RODAK_CHECK(waited);
    RODAK_CHECK(saved.load());
    RODAK_CHECK_EQ(f.Read(path), camera_host::EncodedBytes());
    RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
}

RODAK_TEST("capture retries preserve local and remote preview leases and release fake hardware") {
    Fixture f; f.Preview();
    RODAK_CHECK(f.camera->StartPreview(CameraService::PreviewOwner::kRemote, 2, 2));
    camera_host::fail_flush = true;
    std::string path;
    RODAK_CHECK_FALSE(f.camera->CapturePhoto(path));
    RODAK_CHECK_FALSE(f.Save().empty());
    f.camera->StopPreview(CameraService::PreviewOwner::kLocal);
    RODAK_CHECK(f.camera->GetState().preview_running);
    RODAK_CHECK(f.camera->GetState().has_frame);
    RODAK_CHECK(camera_host::preview_frame_bytes.load() >= 8u);
    f.camera->StopPreview(CameraService::PreviewOwner::kRemote);
    camera_host::JoinTasks();
    RODAK_CHECK_FALSE(f.camera->GetState().preview_running);
    RODAK_CHECK_FALSE(f.camera->GetState().has_frame);
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
    RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
}

RODAK_TEST("final preview stop releases its owned pixels and rejects stale capture before restart") {
    Fixture f; f.Preview();
    CameraFrame snapshot;
    RODAK_CHECK(f.camera->GetLatestFrame(snapshot));
    const auto pixels = snapshot.rgb565;
    RODAK_CHECK_EQ(pixels.size(), 8u);
    RODAK_CHECK(camera_host::preview_frame_bytes.load() >= pixels.size());

    f.camera->StopPreview();
    camera_host::JoinTasks();
    RODAK_CHECK_FALSE(f.camera->GetState().has_frame);
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
    CameraFrame stopped_frame;
    RODAK_CHECK_FALSE(f.camera->GetLatestFrame(stopped_frame));
    Bytes jpeg{1, 2, 3};
    RODAK_CHECK_FALSE(f.camera->CaptureJpeg(jpeg));
    RODAK_CHECK(jpeg.empty());
    RODAK_CHECK_EQ(snapshot.rgb565, pixels);

    f.Preview();
    RODAK_CHECK(f.camera->GetLatestFrame(stopped_frame));
    RODAK_CHECK_EQ(stopped_frame.rgb565, pixels);
    RODAK_CHECK(f.camera->CaptureJpeg(jpeg));
    f.camera->StopPreview();
    camera_host::JoinTasks();
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
}

RODAK_TEST("stopping a remote preview owner retains the local owner's live frame") {
    Fixture f; f.Preview();
    RODAK_CHECK(f.camera->StartPreview(CameraService::PreviewOwner::kRemote, 2, 2));
    f.camera->StopPreview(CameraService::PreviewOwner::kRemote);
    CameraFrame frame;
    RODAK_CHECK(f.camera->GetState().preview_running);
    RODAK_CHECK(f.camera->GetLatestFrame(frame));
    RODAK_CHECK(camera_host::preview_frame_bytes.load() >= 8u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 2u);
    f.camera->StopPreview(CameraService::PreviewOwner::kRemote);
    RODAK_CHECK(f.camera->GetLatestFrame(frame));
    f.camera->StopPreview();
    camera_host::JoinTasks();
    RODAK_CHECK_FALSE(f.camera->GetLatestFrame(frame));
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
}

RODAK_TEST("unexpected dequeue failure revokes both preview leases and releases the last frame") {
    Fixture f; f.Preview();
    RODAK_CHECK(f.camera->StartPreview(CameraService::PreviewOwner::kRemote, 2, 2));
    camera_host::fail_dequeue = true;
    for (unsigned attempt = 0; attempt < 1000 && f.camera->GetState().preview_running; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    RODAK_CHECK_FALSE(f.camera->GetState().preview_running);
    camera_host::JoinTasks();
    RODAK_CHECK_FALSE(f.camera->GetState().has_frame);
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
    RODAK_CHECK(f.camera->last_error().find("dequeue failed") != std::string::npos);
    CameraFrame frame;
    RODAK_CHECK_FALSE(f.camera->GetLatestFrame(frame));

    camera_host::fail_dequeue = false;
    f.Preview();
    f.camera->StopPreview();
    camera_host::JoinTasks();
    RODAK_CHECK_FALSE(f.camera->GetState().preview_running);
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
}

RODAK_TEST("concurrent frame readers own their copies across final preview stop") {
    Fixture f; f.Preview();
    std::atomic<bool> done{false}, valid{true};
    std::atomic<unsigned> snapshots{0};
    auto reader = std::async(std::launch::async, [&]() {
        while (!done) {
            CameraFrame frame;
            if (f.camera->GetLatestFrame(frame)) {
                ++snapshots;
                std::this_thread::yield();
                if (frame.width != 2 || frame.height != 2 || frame.rgb565 != Bytes(8, 0xff)) {
                    valid = false;
                }
            }
        }
    });
    while (snapshots == 0) std::this_thread::yield();
    f.camera->StopPreview();
    bool rejected_stale_frame = true;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        CameraFrame frame;
        rejected_stale_frame &= !f.camera->GetLatestFrame(frame);
    }
    done = true;
    reader.get();
    camera_host::JoinTasks();
    RODAK_CHECK(valid.load());
    RODAK_CHECK(rejected_stale_frame);
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
}

RODAK_TEST("preview stop waits for the worker's final service access before publishing completion") {
    Fixture f; f.Preview();
    std::promise<void> entered, release;
    const auto entered_future = entered.get_future();
    const auto gate = release.get_future().share();
    camera_host::preview_state_query_hook = [&]() { entered.set_value(); gate.wait(); };
    auto stop = std::async(std::launch::async, [&]() { f.camera->StopPreview(); });
    const bool reached_tail = entered_future.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    const bool waited = stop.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    release.set_value();
    stop.get();
    camera_host::JoinTasks();
    camera_host::preview_state_query_hook = {};
    RODAK_CHECK(reached_tail);
    RODAK_CHECK(waited);
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
}

RODAK_TEST("camera destruction revokes every preview lease before deleting service state") {
    Fixture f; f.Preview();
    RODAK_CHECK(f.camera->StartPreview(CameraService::PreviewOwner::kRemote, 2, 2));
    f.camera->StopPreview(CameraService::PreviewOwner::kLocal);
    RODAK_CHECK(f.camera->GetState().preview_running);
    f.camera.reset();
    camera_host::JoinTasks();
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
}

RODAK_TEST("file service write leases reject overlapping mutators without blocking reads") {
    Fixture f;
    RODAK_CHECK(f.files->Init());
    RODAK_CHECK(f.files->CreateDirectory("/recordings"));
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    auto lease = std::async(std::launch::async, [&]() {
        return f.files->WithWriteLease("/recordings/clip.wav", [&]() {
            entered.set_value();
            gate.wait();
            return true;
        });
    });
    entered.get_future().wait();
    RODAK_CHECK_FALSE(f.files->WriteNewFile("/recordings/clip.wav", {1, 2}));
    RODAK_CHECK_EQ(errno, EBUSY);
    RODAK_CHECK_FALSE(f.files->DeleteDirectory("/recordings"));
    RODAK_CHECK_EQ(errno, EBUSY);
    RODAK_CHECK(f.files->Exists("/recordings"));
    release.set_value();
    RODAK_CHECK(lease.get());
    RODAK_CHECK(f.files->WriteNewFile("/recordings/clip.wav", {1, 2}));
}

RODAK_TEST("directory read without a mounted device clears stale entries and reports ENODEV") {
    Fixture f;
    std::vector<FileEntry> entries{{"stale", "/stale", false, 0, 0}};
    errno = ENOENT;
    RODAK_CHECK_FALSE(f.files->ListDirectory("/", entries));
    RODAK_CHECK_EQ(errno, ENODEV);
    RODAK_CHECK(entries.empty());
}

RODAK_TEST("directory reader preserves missing-directory errno through adapter logging") {
    Fixture f;
    RODAK_CHECK(f.files->Init());
    std::vector<FileEntry> entries{{"stale", "/stale", false, 0, 0}};
    errno = EBUSY;
    RODAK_CHECK_FALSE(f.files->ListDirectory("/missing-folder", entries));
    RODAK_CHECK_EQ(errno, ENOENT);
    RODAK_CHECK(entries.empty());
}

namespace {
template <typename F> bool WaitFor(F&& predicate) {
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}
using AllocationThread = camera_host::AllocationThread;
}

RODAK_TEST("preview allocation failures requeue buffers and recover without revoking owners") {
    Fixture f; f.Preview();
    RODAK_CHECK(f.camera->StartPreview(CameraService::PreviewOwner::kRemote, 2, 2));
    CameraFrame held;
    RODAK_CHECK(f.camera->GetLatestFrame(held));
    const auto original = held.rgb565;
    camera_host::FailNew(8, 1, SIZE_MAX, AllocationThread::kPreview);
    RODAK_CHECK(WaitFor([] { return camera_host::new_failures >= 3; }));
    RODAK_CHECK(f.camera->GetState().preview_running);
    RODAK_CHECK(f.camera->GetState().has_frame);
    RODAK_CHECK_EQ(f.camera->last_error(), "Camera OOM");
    f.camera->StopPreview(CameraService::PreviewOwner::kLocal);
    RODAK_CHECK(f.camera->GetState().preview_running);
    camera_host::ClearNewFailures();
    const auto count = f.camera->GetState().frame_count;
    RODAK_CHECK(WaitFor([&] { return f.camera->GetState().frame_count > count; }));
    f.camera->StopPreview(CameraService::PreviewOwner::kRemote);
    camera_host::JoinTasks();
    RODAK_CHECK_EQ(held.rgb565, original);
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
    RODAK_CHECK_EQ(camera_host::requeued_buffers.load(), camera_host::dequeued_buffers.load() + 2u);
    f.Preview();
}

RODAK_TEST("first preview frame allocation failures recover on a later driver buffer") {
    Fixture f;
    camera_host::FailNew(8, 1, 3, AllocationThread::kPreview);
    f.Preview();
    RODAK_CHECK_EQ(camera_host::new_failures.load(), 3u);
    f.camera->StopPreview();
    camera_host::JoinTasks();
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
    RODAK_CHECK_EQ(camera_host::requeued_buffers.load(), camera_host::dequeued_buffers.load() + 2u);
}

RODAK_TEST("snapshot allocation failure releases the lock and preserves caller owned pixels") {
    Fixture f; f.Preview();
    CameraFrame held;
    held.width = held.height = 1;
    held.stride = 2;
    held.rgb565 = {0xab, 0xcd};
    held.sequence = 7;
    const auto sequence = held.sequence;
    const auto pixels = held.rgb565;
    // Continue rejecting allocations while the catch records its short error.
    camera_host::FailNew(SIZE_MAX, 1, SIZE_MAX, AllocationThread::kCaller);
    const bool copied = f.camera->GetLatestFrame(held);
    camera_host::ClearNewFailures();
    RODAK_CHECK_FALSE(copied);
    RODAK_CHECK_EQ(camera_host::new_failures.load(), 1u);
    RODAK_CHECK_EQ(held.sequence, sequence);
    RODAK_CHECK_EQ(held.rgb565, pixels);
    RODAK_CHECK_EQ(f.camera->last_error(), "Camera OOM");
    RODAK_CHECK(f.camera->GetLatestFrame(held));
    f.camera->StopPreview();
}

RODAK_TEST("camera JPEG allocation failures release encoder input and preserve preview") {
    Fixture f; f.Preview();
    for (const auto& [bytes, nth] : {std::pair<size_t, size_t>{8, 1}, {8, 2}, {65536, 1}}) {
        Bytes jpeg{1, 2, 3};
        camera_host::FailNew(bytes, nth, 1, AllocationThread::kCaller);
        const bool encoded = f.camera->CaptureJpeg(jpeg);
        camera_host::ClearNewFailures();
        RODAK_CHECK_FALSE(encoded);
        RODAK_CHECK(jpeg.empty());
        RODAK_CHECK_EQ(f.camera->last_error(), "Camera OOM");
        RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
        RODAK_CHECK_EQ(camera_host::aligned_buffers.load(), 0u);
        RODAK_CHECK(f.camera->GetState().preview_running);
        RODAK_CHECK(f.camera->CaptureJpeg(jpeg));
    }
    RODAK_CHECK_EQ(camera_host::new_failures.load(), 3u);
}

RODAK_TEST("camera state and error snapshots keep locks usable during repeated allocation failure") {
    Fixture f; f.Preview(); f.Save();
    camera_host::FailNew(SIZE_MAX, 1, SIZE_MAX, AllocationThread::kCaller);
    auto state = f.camera->GetState();
    camera_host::ClearNewFailures();
    RODAK_CHECK(state.preview_running);
    RODAK_CHECK(state.has_frame);
    RODAK_CHECK(state.last_saved_path.empty());
    RODAK_CHECK_EQ(state.last_error, "Camera OOM");
    f.camera->StopPreview();
    Bytes jpeg;
    RODAK_CHECK_FALSE(f.camera->CaptureJpeg(jpeg));
    camera_host::FailNew(SIZE_MAX, 1, SIZE_MAX, AllocationThread::kCaller);
    auto error = f.camera->last_error();
    camera_host::ClearNewFailures();
    RODAK_CHECK_EQ(error, "Camera OOM");
    f.Preview();
}

RODAK_TEST("camera JPEG worker recovers from callback copy allocation failure without holding its lock") {
    Fixture f; f.Preview();
    std::atomic<unsigned> delivered{0};
    struct Callback {
        std::array<uint8_t, 128> storage{};
        std::atomic<unsigned>* delivered;
        void operator()(Bytes&&, uint32_t, int64_t) const { ++*delivered; }
    };
    camera_host::FailNew(sizeof(Callback), 1, 3, AllocationThread::kJpeg);
    RODAK_CHECK(f.camera->StartJpegStream(30, Callback{{}, &delivered}));
    RODAK_CHECK(WaitFor([&] { return delivered > 0; }));
    f.camera->StopJpegStream();
    RODAK_CHECK_EQ(camera_host::new_failures.load(), 3u);
    RODAK_CHECK_EQ(f.camera->last_error(), "Camera OOM");
    RODAK_CHECK(f.camera->GetState().preview_running);
    RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
    RODAK_CHECK_EQ(camera_host::aligned_buffers.load(), 0u);
}

RODAK_TEST("camera JPEG callback bad_alloc consumes its sequence and permits later frames") {
    Fixture f; f.Preview();
    camera_host::pause_frames = true;
    RODAK_CHECK(WaitFor([] { return camera_host::frames_paused.load(); }));
    std::atomic<unsigned> attempts{0}, delivered{0};
    std::atomic<uint32_t> rejected{0}, accepted{0};
    RODAK_CHECK(f.camera->StartJpegStream(30, [&](Bytes&&, uint32_t sequence, int64_t) {
        if (attempts++ == 0) { rejected = sequence; throw std::bad_alloc(); }
        accepted = sequence;
        ++delivered;
    }));
    RODAK_CHECK(WaitFor([&] { return attempts > 0; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    const auto attempts_before_new_frame = attempts.load();
    camera_host::pause_frames = false;
    RODAK_CHECK(WaitFor([&] { return delivered > 0; }));
    f.camera->StopJpegStream();
    RODAK_CHECK_EQ(attempts_before_new_frame, 1u);
    RODAK_CHECK(accepted > rejected);
    RODAK_CHECK_EQ(f.camera->last_error(), "Camera OOM");
    RODAK_CHECK(f.camera->GetState().preview_running);
    RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
    RODAK_CHECK_EQ(camera_host::aligned_buffers.load(), 0u);
}

RODAK_TEST("camera JPEG stop remains observable during persistent callback copy failure") {
    Fixture f; f.Preview();
    struct Callback {
        std::array<uint8_t, 128> storage{};
        void operator()(Bytes&&, uint32_t, int64_t) const {}
    };
    camera_host::FailNew(sizeof(Callback), 1, SIZE_MAX, AllocationThread::kJpeg);
    RODAK_CHECK(f.camera->StartJpegStream(30, Callback{}));
    RODAK_CHECK(WaitFor([] { return camera_host::new_failures >= 3; }));
    const auto start = std::chrono::steady_clock::now();
    f.camera->StopJpegStream();
    camera_host::ClearNewFailures();
    RODAK_CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    RODAK_CHECK(f.camera->GetState().preview_running);
}

RODAK_TEST("camera video buffer table allocation failure closes the device and permits retry") {
    Fixture f;
    camera_host::FailNew(2 * (sizeof(void*) + sizeof(size_t)), 1, 1, AllocationThread::kCaller);
    const bool started = f.camera->StartPreview(2, 2);
    camera_host::ClearNewFailures();
    RODAK_CHECK_FALSE(started);
    RODAK_CHECK_EQ(camera_host::new_failures.load(), 1u);
    RODAK_CHECK_EQ(f.camera->last_error(), "Camera OOM");
    RODAK_CHECK_FALSE(f.camera->GetState().preview_running);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
    f.Preview();
}

RODAK_TEST("camera STREAMON failure releases unopened stream resources and permits retry") {
    Fixture f;
    camera_host::streamon_result = -1;
    camera_host::streamoff_result = -1;
    RODAK_CHECK_FALSE(f.camera->StartPreview(2, 2));
    RODAK_CHECK_EQ(camera_host::streamon_calls.load(), 1u);
    RODAK_CHECK_EQ(camera_host::streamoff_calls.load(), 0u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
    RODAK_CHECK_FALSE(f.camera->GetState().preview_running);
    RODAK_CHECK(f.camera->last_error().find("Failed to start camera stream") != std::string::npos);

    camera_host::streamon_result = 0;
    camera_host::streamoff_result = 0;
    f.Preview();
    f.camera->StopPreview();
    camera_host::JoinTasks();
    RODAK_CHECK_EQ(camera_host::streamon_calls.load(), 2u);
    RODAK_CHECK_EQ(camera_host::streamoff_calls.load(), 1u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
}

RODAK_TEST("camera successful file write publishes results without allocating afterward") {
    Fixture f; f.Preview();
    camera_host::write_hook = [] {
        camera_host::FailNew(SIZE_MAX, 1, SIZE_MAX, AllocationThread::kCaller);
    };
    std::string path;
    const bool saved = f.camera->CapturePhoto(path);
    camera_host::ClearNewFailures();
    camera_host::write_hook = {};
    RODAK_CHECK(saved);
    RODAK_CHECK_FALSE(path.empty());
    RODAK_CHECK_EQ(f.camera->GetState().last_saved_path, path);
    RODAK_CHECK_EQ(f.Read(path), camera_host::EncodedBytes());
    RODAK_CHECK_EQ(camera_host::new_failures.load(), 0u);
    RODAK_CHECK_FALSE(f.Save().empty());
}

RODAK_TEST("camera save allocation rejection preserves historical result and permits retry") {
    Fixture f; f.Preview();
    const auto previous = f.Save();
    camera_host::FailNew(previous.size() + 1, 1, 1, AllocationThread::kCaller);
    std::string path;
    const bool saved = f.camera->CapturePhoto(path);
    camera_host::ClearNewFailures();
    RODAK_CHECK_FALSE(saved);
    RODAK_CHECK(path.empty());
    RODAK_CHECK_EQ(camera_host::new_failures.load(), 1u);
    RODAK_CHECK_EQ(f.camera->GetState().last_saved_path, previous);
    RODAK_CHECK_EQ(f.PhotoCount(), 1u);
    RODAK_CHECK_EQ(f.Read(previous), camera_host::EncodedBytes());
    RODAK_CHECK_FALSE(f.Save().empty());
}

RODAK_TEST("camera JPEG worker snapshot OOM drops attempts then recovers with resources released") {
    Fixture f; f.Preview();
    std::atomic<unsigned> delivered{0};
    camera_host::FailNew(8, 1, 3, AllocationThread::kJpeg);
    RODAK_CHECK(f.camera->StartJpegStream(30, [&](Bytes&&, uint32_t, int64_t) { ++delivered; }));
    RODAK_CHECK(WaitFor([&] { return delivered > 0; }));
    f.camera->StopJpegStream();
    RODAK_CHECK_EQ(camera_host::new_failures.load(), 3u);
    RODAK_CHECK_EQ(camera_host::encoder_handles.load(), 0u);
    RODAK_CHECK_EQ(camera_host::aligned_buffers.load(), 0u);
    RODAK_CHECK(f.camera->GetState().preview_running);
}

RODAK_TEST("camera WithCaps workers reclaim their buffers without exit allocation or cleanup tasks") {
    Fixture f; f.Preview();
    std::atomic<unsigned> delivered{0};
    RODAK_CHECK(f.camera->StartJpegStream(30, [&](Bytes&&, uint32_t, int64_t) { ++delivered; }));
    RODAK_CHECK(WaitFor([&] { return delivered > 0; }));
    retirement_host::RejectCleanupTask(true);
    const auto before = retirement_host::Snapshot();
    f.camera->StopJpegStream();
    f.camera->StopPreview();
    const auto after = retirement_host::Snapshot();
    RODAK_CHECK_EQ(after.task_deletes - before.task_deletes, 2u);
    RODAK_CHECK_EQ(after.live_tasks, 0u);
    RODAK_CHECK_EQ(after.live_task_buffers, 0u);
    RODAK_CHECK_EQ(after.allocation_calls, before.allocation_calls);
    RODAK_CHECK_EQ(after.cleanup_create_attempts, 0u);
}

RODAK_TEST("camera task creation failures cancel reserved generations and permit retry") {
    Fixture f;
    retirement_host::SetCreationAllowed(false);
    RODAK_CHECK_FALSE(f.camera->StartPreview(2, 2));
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
    retirement_host::SetCreationAllowed(true);
    f.Preview();
    retirement_host::SetCreationAllowed(false);
    RODAK_CHECK_FALSE(f.camera->StartJpegStream(30, [](Bytes&&, uint32_t, int64_t) {}));
    RODAK_CHECK(f.camera->GetState().preview_running);
    retirement_host::SetCreationAllowed(true);
    RODAK_CHECK(f.camera->StartJpegStream(30, [](Bytes&&, uint32_t, int64_t) {}));
    f.camera->StopJpegStream();
    f.camera->StopPreview();
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
}

namespace {
std::atomic<unsigned> publication_hook_calls{0};
std::atomic<bool> preview_entered_before_publication{false};
void ObservePreviewPublication(TaskHandle_t) {
    ++publication_hook_calls;
    preview_entered_before_publication = camera_host::dequeued_buffers != 0;
}
}

RODAK_TEST("camera preview scheduled inside real WithCaps creation waits for handle publication") {
    Fixture f;
    publication_hook_calls = 0;
    preview_entered_before_publication = false;
    retirement_host::SetBeforeCreateReturnsHook(ObservePreviewPublication);
    f.Preview();
    retirement_host::SetBeforeCreateReturnsHook(nullptr);
    f.camera->StopPreview();
    RODAK_CHECK_EQ(publication_hook_calls.load(), 1u);
    RODAK_CHECK_FALSE(preview_entered_before_publication.load());
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
}

RODAK_TEST("concurrent camera JPEG Stop callers wait for one generation and reclaim once") {
    Fixture f; f.Preview();
    retirement_host::Gate callback;
    RODAK_CHECK(f.camera->StartJpegStream(30, [&](Bytes&&, uint32_t, int64_t) {
        callback.Enter();
    }));
    const bool entered = callback.Wait();
    auto first = std::async(std::launch::async, [&] { f.camera->StopJpegStream(); });
    auto second = std::async(std::launch::async, [&] { f.camera->StopJpegStream(); });
    const bool first_waited = first.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    const bool second_waited = second.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    callback.Release();
    first.get();
    second.get();
    RODAK_CHECK(entered);
    RODAK_CHECK(first_waited);
    RODAK_CHECK(second_waited);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_tasks, 1u);
}

RODAK_TEST("camera JPEG callback self Stop returns and permanent owner pump reclaims autonomously") {
    Fixture f; f.Preview();
    std::atomic<bool> returned{false};
    const auto before = retirement_host::Snapshot();
    RODAK_CHECK(f.camera->StartJpegStream(30, [&](Bytes&&, uint32_t, int64_t) {
        f.camera->StopJpegStream();
        returned = true;
    }));
    RODAK_CHECK(WaitFor([&] { return returned.load(); }));
    // No external Stop/Start/Join is used to make autonomous retirement happen.
    RODAK_CHECK(WaitFor([&] {
        PumpTaskRetirements();
        return retirement_host::Snapshot().task_deletes == before.task_deletes + 1;
    }));
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_tasks, 1u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().cleanup_create_attempts, 0u);
}

RODAK_TEST("camera autonomous preview failure is reclaimed by the permanent owner pump") {
    Fixture f; f.Preview();
    camera_host::fail_dequeue = true;
    RODAK_CHECK(WaitFor([&] { return !f.camera->GetState().preview_running; }));
    RODAK_CHECK(WaitFor([] {
        PumpTaskRetirements();
        return retirement_host::Snapshot().live_tasks == 0;
    }));
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
}

RODAK_TEST("camera JPEG callback destruction can start a replacement without extending old Stop") {
    Fixture f; f.Preview();
    retirement_host::Gate old_callback, replacement_callback;
    std::atomic<bool> replaced{false};
    struct Restart {
        CameraService* service;
        retirement_host::Gate* replacement;
        std::atomic<bool>* replaced;
        ~Restart() {
            *replaced = service->StartJpegStream(30, [gate = replacement](Bytes&&, uint32_t, int64_t) {
                gate->Enter();
            });
        }
    };
    auto capture = std::make_shared<Restart>();
    capture->service = f.camera.get();
    capture->replacement = &replacement_callback;
    capture->replaced = &replaced;
    RODAK_CHECK(f.camera->StartJpegStream(30, [capture, &old_callback](Bytes&&, uint32_t, int64_t) {
        old_callback.Enter();
    }));
    capture.reset();
    const bool entered = old_callback.Wait();
    auto old_stop = std::async(std::launch::async, [&] { f.camera->StopJpegStream(); });
    const bool waited = old_stop.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    old_callback.Release();
    const bool replacement_entered = replacement_callback.Wait();
    const bool old_completed = old_stop.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    replacement_callback.Release();
    old_stop.get();
    f.camera->StopJpegStream();
    RODAK_CHECK(entered);
    RODAK_CHECK(waited);
    RODAK_CHECK(replaced.load());
    RODAK_CHECK(replacement_entered);
    RODAK_CHECK(old_completed);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 2u);
}

RODAK_TEST("camera late JPEG Stop still waits for the final callback capture destructor") {
    Fixture f; f.Preview();
    retirement_host::Gate destructor;
    struct Capture {
        retirement_host::Gate* gate;
        ~Capture() { gate->Enter(); }
    };
    auto capture = std::make_shared<Capture>();
    capture->gate = &destructor;
    std::atomic<bool> delivered{false};
    RODAK_CHECK(f.camera->StartJpegStream(30, [capture, &delivered](Bytes&&, uint32_t, int64_t) {
        delivered = true;
    }));
    capture.reset();
    RODAK_CHECK(WaitFor([&] { return delivered.load(); }));
    auto first = std::async(std::launch::async, [&] { f.camera->StopJpegStream(); });
    const bool destructor_entered = destructor.Wait();
    // Logical completion is already visible here. The retained most recent
    // ticket must still join the old worker's unfinished local destructor.
    auto late = std::async(std::launch::async, [&] { f.camera->StopJpegStream(); });
    const bool first_waited = first.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    const bool late_waited = late.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    const auto before_release = retirement_host::Snapshot().task_deletes;
    destructor.Release();
    first.get();
    late.get();
    RODAK_CHECK(destructor_entered);
    RODAK_CHECK(first_waited);
    RODAK_CHECK(late_waited);
    RODAK_CHECK_EQ(before_release, 0u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
}

RODAK_TEST("camera preview self Stop can reenter while an external Stop joins the same worker") {
    Fixture f; f.Preview();
    std::atomic<bool> self_stop_returned{false};
    camera_host::preview_state_query_hook = [&] {
        f.camera->StopPreview();
        self_stop_returned = true;
    };
    f.camera->StopPreview();
    camera_host::preview_state_query_hook = {};
    RODAK_CHECK(self_stop_returned.load());
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
}

RODAK_TEST("camera destruction closes admission before releasing callback captures") {
    Fixture f; f.Preview();
    std::atomic<bool> rejected{false}, preview_rejected{false};
    struct Restart {
        CameraService* service;
        std::atomic<bool>* rejected;
        std::atomic<bool>* preview_rejected;
        ~Restart() {
            *rejected = !service->StartJpegStream(30, [](Bytes&&, uint32_t, int64_t) {});
            // The old preview still runs here and already owns this lease;
            // Close must reject even the otherwise successful fast path.
            *preview_rejected = !service->StartPreview(2, 2);
        }
    };
    auto capture = std::make_shared<Restart>();
    capture->service = f.camera.get();
    capture->rejected = &rejected;
    capture->preview_rejected = &preview_rejected;
    RODAK_CHECK(f.camera->StartJpegStream(30, [capture](Bytes&&, uint32_t, int64_t) {}));
    capture.reset();
    f.camera.reset();
    RODAK_CHECK(rejected.load());
    RODAK_CHECK(preview_rejected.load());
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_tasks, 0u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
}

RODAK_TEST("failed camera replacement creation preserves the unfinished previous generation for late Stop") {
    Fixture f; f.Preview();
    retirement_host::Gate destructor;
    std::atomic<bool> rejected{false};
    struct Capture {
        CameraService* service;
        retirement_host::Gate* gate;
        std::atomic<bool>* rejected;
        ~Capture() {
            retirement_host::SetCreationAllowed(false);
            *rejected = !service->StartJpegStream(30, [](Bytes&&, uint32_t, int64_t) {});
            retirement_host::SetCreationAllowed(true);
            gate->Enter();
        }
    };
    auto capture = std::make_shared<Capture>();
    capture->service = f.camera.get();
    capture->gate = &destructor;
    capture->rejected = &rejected;
    RODAK_CHECK(f.camera->StartJpegStream(30, [capture](Bytes&&, uint32_t, int64_t) {}));
    capture.reset();
    auto first = std::async(std::launch::async, [&] { f.camera->StopJpegStream(); });
    const bool entered = destructor.Wait();
    auto late = std::async(std::launch::async, [&] { f.camera->StopJpegStream(); });
    const bool waited = late.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    destructor.Release();
    first.get();
    late.get();
    RODAK_CHECK(entered);
    RODAK_CHECK(rejected.load());
    RODAK_CHECK(waited);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 2u);
}

RODAK_TEST("failed preview replacement preserves the old task until real IDF core convergence") {
    Fixture f; f.Preview();
    retirement_host::Gate old_core;
    retirement_host::HoldCoreAfterSuspend(&old_core);
    camera_host::fail_dequeue = true;
    const bool exited_body = old_core.Wait();
    camera_host::fail_dequeue = false;
    retirement_host::SetCreationAllowed(false);
    const bool rejected = !f.camera->StartPreview(2, 2);
    retirement_host::SetCreationAllowed(true);
    auto late = std::async(std::launch::async, [&] { f.camera->StopPreview(); });
    const bool waited = late.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    const auto deleted_before_convergence = retirement_host::Snapshot().task_deletes;
    old_core.Release();
    late.get();
    retirement_host::HoldCoreAfterSuspend(nullptr);
    RODAK_CHECK(exited_body);
    RODAK_CHECK(rejected);
    RODAK_CHECK(waited);
    RODAK_CHECK_EQ(deleted_before_convergence, 0u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
}
