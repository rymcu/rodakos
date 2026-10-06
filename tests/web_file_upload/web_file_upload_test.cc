#include "test_framework.h"
#define private public
#include "phone_os/web_file_system_service.h"
#undef private
#include "rodakos_adapters/file_service.h"
#include "esp_http_server.h"
#include "web_stdio_faults.h"
#include <cerrno>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
class FakeFiles final : public rodakos::FileService {
public:
    bool Init() override { mounted = true; return true; }
    void Deinit() override { mounted = false; }
    bool IsMounted() const override { return mounted; }
    const char* GetMountPoint() const override { return root.c_str(); }
    FileSystemType GetFileSystemType() const override { return FileSystemType::FATFS; }
    MediumType GetMediumType() const override { return MediumType::SDCard; }
    bool GetCapacity(Capacity&) override { return true; }
    bool ListDirectory(const std::string&, std::vector<rodakos::FileEntry>& entries) override { entries.clear(); return true; }
    bool ReadFile(const std::string&, std::vector<uint8_t>&) override { return false; }
    bool WriteFile(const std::string&, const std::vector<uint8_t>&, bool) override { return false; }
    bool WriteNewFile(const std::string&, const std::vector<uint8_t>&) override { return false; }
    bool DeleteFile(const std::string& path) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (lease_active && path == leased_path) { errno = EBUSY; return false; }
        return true;
    }
    bool DeleteDirectory(const std::string&) override { return false; }
    bool CreateDirectory(const std::string&) override { return true; }
    bool Rename(const std::string& from, const std::string& to) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (lease_active && (from == leased_path || to == leased_path)) { errno = EBUSY; return false; }
        return true;
    }
    bool Exists(const std::string&) override { return true; }
    size_t GetFileSize(const std::string&) override { return 0; }
    bool WithWriteLease(const std::string& path, const std::function<bool()>& operation) override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (lease_active) { errno = EBUSY; return false; }
            lease_active = true; leased_path = path;
        }
        if (wait_for_release) {
            entered_cv.notify_all();
            std::unique_lock<std::mutex> gate_lock(mutex);
            entered_cv.wait(gate_lock, [&] { return release; });
            gate_lock.unlock();
        }
        const bool result = operation();
        std::lock_guard<std::mutex> lock(mutex);
        lease_active = false; leased_path.clear(); release = false;
        return result;
    }
    std::string root;
    bool mounted = true;
    std::mutex mutex;
    std::condition_variable entered_cv;
    bool lease_active = false;
    bool release = false;
    bool wait_for_release = false;
    std::string leased_path;
};
httpd_req_t Request(rodakos::WebFileSystemService& service, const std::string& body = "payload") {
    httpd_req_t req;
    req.user_ctx = &service;
    req.content_len = static_cast<int>(body.size());
    req.body.assign(body.begin(), body.end());
    req.query = "token=token&path=%2Frecordings%2Fupload.bin";
    return req;
}
void Release(FakeFiles& files) {
    std::lock_guard<std::mutex> lock(files.mutex);
    files.release = true;
    files.entered_cv.notify_all();
}
}

RODAK_TEST("production UploadHandler returns 409 when recording lease owns destination") {
    FakeFiles files; files.root = (std::filesystem::temp_directory_path() / "rodakos-web-upload").string();
    std::filesystem::create_directories(files.root);
    rodakos::WebFileSystemService service(&files);
    service.access_token_ = "token";
    service.state_.running = true;
    {
        std::lock_guard<std::mutex> lock(files.mutex);
        files.lease_active = true;
        files.leased_path = "/recordings/upload.bin";
    }
    auto request = Request(service);
    const auto result = rodakos::WebFileSystemService::UploadHandler(&request);
    RODAK_CHECK_EQ(result, ESP_FAIL);
    RODAK_CHECK_EQ(request.response_status, "409 Conflict");
    {
        std::lock_guard<std::mutex> lock(files.mutex);
        files.lease_active = false;
    }
    std::filesystem::remove_all(files.root);
}

RODAK_TEST("production UploadHandler keeps lease through body and responds OK") {
    FakeFiles files; files.root = (std::filesystem::temp_directory_path() / "rodakos-web-upload-ok").string();
    std::filesystem::create_directories(files.root + "/recordings");
    rodakos::WebFileSystemService service(&files);
    service.access_token_ = "token";
    service.state_.running = true;
    files.wait_for_release = true;
    auto request = Request(service);
    std::atomic<bool> done{false};
    esp_err_t result = ESP_FAIL;
    std::thread upload([&] { result = rodakos::WebFileSystemService::UploadHandler(&request); done = true; });
    {
        std::unique_lock<std::mutex> lock(files.mutex);
        RODAK_CHECK(files.entered_cv.wait_for(lock, std::chrono::seconds(2), [&] { return files.lease_active; }));
    }
    RODAK_CHECK_FALSE(files.DeleteFile("/recordings/upload.bin"));
    RODAK_CHECK_FALSE(files.Rename("/recordings/upload.bin", "/recordings/renamed.bin"));
    Release(files);
    upload.join();
    RODAK_CHECK(done.load());
    RODAK_CHECK_EQ(result, ESP_OK);
    RODAK_CHECK_EQ(request.response_body, "OK");
    std::filesystem::remove_all(files.root);
}

RODAK_TEST("production UploadHandler rejects truncated body and cleans partial file") {
    FakeFiles files; files.root = (std::filesystem::temp_directory_path() / "rodakos-web-upload-fail").string();
    std::filesystem::create_directories(files.root);
    rodakos::WebFileSystemService service(&files);
    service.access_token_ = "token";
    service.state_.running = true;
    auto request = Request(service, "x");
    request.content_len = 3;
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::UploadHandler(&request), ESP_FAIL);
    RODAK_CHECK_EQ(request.response_status, "500 Internal Server Error");
    RODAK_CHECK_FALSE(std::filesystem::exists(files.root + "/recordings/upload.bin"));
    std::filesystem::remove_all(files.root);
}

RODAK_TEST("production UploadHandler reports flush failure and removes partial file") {
    FakeFiles files; files.root = (std::filesystem::temp_directory_path() / "rodakos-web-upload-flush").string();
    std::filesystem::create_directories(files.root + "/recordings");
    rodakos::WebFileSystemService service(&files); service.access_token_ = "token"; service.state_.running = true;
    web_upload_test::SetFaults(true, false);
    auto request = Request(service, "flush");
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::UploadHandler(&request), ESP_FAIL);
    RODAK_CHECK_EQ(request.response_status, "500 Internal Server Error");
    RODAK_CHECK_FALSE(std::filesystem::exists(files.root + "/recordings/upload.bin"));
    web_upload_test::ResetFaults(); std::filesystem::remove_all(files.root);
}
RODAK_TEST("production UploadHandler reports close failure and removes partial file") {
    FakeFiles files; files.root = (std::filesystem::temp_directory_path() / "rodakos-web-upload-close").string();
    std::filesystem::create_directories(files.root + "/recordings");
    rodakos::WebFileSystemService service(&files); service.access_token_ = "token"; service.state_.running = true;
    web_upload_test::SetFaults(false, true);
    auto request = Request(service, "close");
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::UploadHandler(&request), ESP_FAIL);
    RODAK_CHECK_EQ(request.response_status, "500 Internal Server Error");
    RODAK_CHECK_FALSE(std::filesystem::exists(files.root + "/recordings/upload.bin"));
    web_upload_test::ResetFaults(); std::filesystem::remove_all(files.root);
}

RODAK_TEST("production UploadHandler rejects a stream error after a full fwrite") {
    FakeFiles files; files.root = (std::filesystem::temp_directory_path() / "rodakos-web-upload-error").string();
    std::filesystem::create_directories(files.root + "/recordings");
    rodakos::WebFileSystemService service(&files); service.access_token_ = "token"; service.state_.running = true;
    web_upload_test::SetFaults(false, false, true);
    auto request = Request(service, "stream-error");
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::UploadHandler(&request), ESP_FAIL);
    RODAK_CHECK_EQ(request.response_status, "500 Internal Server Error");
    RODAK_CHECK_FALSE(std::filesystem::exists(files.root + "/recordings/upload.bin"));
    web_upload_test::ResetFaults(); std::filesystem::remove_all(files.root);
}
