#include "test_framework.h"
#include "host_runtime.h"
#include "phone_os/camera_service.h"
#include "rodakos_adapters/file_service.h"

#include <chrono>
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
