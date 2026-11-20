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
    bool ListDirectory(const std::string& path, std::vector<rodakos::FileEntry>& entries) override {
        entries.clear();
        std::error_code ec;
        for (const auto& item : std::filesystem::directory_iterator(root + path, ec)) {
            entries.push_back({item.path().filename().string(), path, item.is_directory(), 0, 0});
        }
        return true;
    }
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
    req.query = "path=%2Frecordings%2Fupload.bin";
    req.request_headers["X-Access-Code"] = service.guard_.code();
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
    service.guard_.Reset();
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
    service.guard_.Reset();
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
    service.guard_.Reset();
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
    rodakos::WebFileSystemService service(&files); service.guard_.Reset(); service.state_.running = true;
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
    rodakos::WebFileSystemService service(&files); service.guard_.Reset(); service.state_.running = true;
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
    rodakos::WebFileSystemService service(&files); service.guard_.Reset(); service.state_.running = true;
    web_upload_test::SetFaults(false, false, true);
    auto request = Request(service, "stream-error");
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::UploadHandler(&request), ESP_FAIL);
    RODAK_CHECK_EQ(request.response_status, "500 Internal Server Error");
    RODAK_CHECK_FALSE(std::filesystem::exists(files.root + "/recordings/upload.bin"));
    web_upload_test::ResetFaults(); std::filesystem::remove_all(files.root);
}

namespace {
uint32_t g_test_peer = 0x6458a8c0;  // 192.168.88.100
uint32_t TestPeer(httpd_req_t*) { return g_test_peer; }

struct AccessFixture {
    FakeFiles files;
    rodakos::WebFileSystemService service{&files};
    AccessFixture() {
        files.root = (std::filesystem::temp_directory_path() / "rodakos-web-access").string();
        std::filesystem::create_directories(files.root + "/Music");
        std::ofstream(files.root + "/Music/a.mp3") << "song";
        service.guard_.Reset();
        service.state_.running = true;
        g_test_peer = 0x6458a8c0;
        rodakos::WebFileSystemService::peer_address_override_ = TestPeer;
    }
    ~AccessFixture() {
        rodakos::WebFileSystemService::peer_address_override_ = nullptr;
        std::filesystem::remove_all(files.root);
    }
    httpd_req_t List(const std::string& code, bool header = true) {
        httpd_req_t req;
        req.user_ctx = &service;
        req.query = "path=%2F";
        if (header && !code.empty()) req.request_headers["X-Access-Code"] = code;
        if (!header) req.query += "&token=" + code;
        rodakos::WebFileSystemService::ListHandler(&req);
        return req;
    }
};
}  // namespace

RODAK_TEST("web access index page is public and sets no-referrer") {
    AccessFixture f;
    httpd_req_t req;
    req.user_ctx = &f.service;
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::IndexHandler(&req), ESP_OK);
    RODAK_CHECK(req.response_status.empty());
    RODAK_CHECK(req.response_body.find("<html") != std::string::npos);
    RODAK_CHECK_EQ(req.response_headers["Referrer-Policy"], std::string("no-referrer"));
}

RODAK_TEST("web access file API requires the code and accepts it as a header or legacy query") {
    AccessFixture f;
    RODAK_CHECK_EQ(f.List("").response_status, std::string("401 Unauthorized"));
    RODAK_CHECK_EQ(f.List("ZZZZZZZZ").response_status, std::string("401 Unauthorized"));
    RODAK_CHECK(f.List(f.service.guard_.DisplayCode()).response_status.empty());
    RODAK_CHECK(f.List(f.service.guard_.code(), false).response_status.empty());
}

RODAK_TEST("web access locks out only the computer that keeps guessing") {
    AccessFixture f;
    for (int attempt = 0; attempt < 4; ++attempt) {
        RODAK_CHECK_EQ(f.List("ZZZZZZZZ").response_status, std::string("401 Unauthorized"));
    }
    const auto locked = f.List("ZZZZZZZZ");
    RODAK_CHECK_EQ(locked.response_status, std::string("429 Too Many Requests"));
    RODAK_CHECK_EQ(locked.response_headers.at("Retry-After"), std::string("30"));
    RODAK_CHECK_EQ(f.List(f.service.guard_.code()).response_status, std::string("429 Too Many Requests"));

    g_test_peer = 0x6558a8c0;  // 192.168.88.101
    RODAK_CHECK(f.List(f.service.guard_.code()).response_status.empty());
}

RODAK_TEST("web access download needs a one-time ticket for that exact file") {
    AccessFixture f;
    httpd_req_t issue;
    issue.user_ctx = &f.service;
    issue.query = "path=%2FMusic%2Fa.mp3";
    issue.request_headers["X-Access-Code"] = f.service.guard_.code();
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::TicketHandler(&issue), ESP_OK);
    const std::string ticket = issue.response_body;
    RODAK_CHECK_EQ(ticket.size(), static_cast<size_t>(16));

    httpd_req_t download;
    download.user_ctx = &f.service;
    download.query = "path=%2FMusic%2Fa.mp3&ticket=" + ticket;
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::DownloadHandler(&download), ESP_OK);
    RODAK_CHECK_EQ(download.response_body, std::string("song"));

    httpd_req_t replay;
    replay.user_ctx = &f.service;
    replay.query = download.query;
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::DownloadHandler(&replay), ESP_FAIL);
    RODAK_CHECK_EQ(replay.response_status, std::string("401 Unauthorized"));

    httpd_req_t anonymous;
    anonymous.user_ctx = &f.service;
    anonymous.query = "path=%2FMusic%2Fa.mp3";
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::DownloadHandler(&anonymous), ESP_FAIL);
    RODAK_CHECK_EQ(anonymous.response_status, std::string("401 Unauthorized"));
}

RODAK_TEST("web access ticket issuing itself requires the code") {
    AccessFixture f;
    httpd_req_t issue;
    issue.user_ctx = &f.service;
    issue.query = "path=%2FMusic%2Fa.mp3";
    RODAK_CHECK_EQ(rodakos::WebFileSystemService::TicketHandler(&issue), ESP_FAIL);
    RODAK_CHECK_EQ(issue.response_status, std::string("401 Unauthorized"));
}

RODAK_TEST("web access index serves the embedded login page without its NUL terminator") {
    AccessFixture f;
    httpd_req_t req;
    req.user_ctx = &f.service;
    rodakos::WebFileSystemService::IndexHandler(&req);
    RODAK_CHECK(req.response_body.find("输入访问码") != std::string::npos);
    RODAK_CHECK(req.response_body.find("X-Access-Code") != std::string::npos);
    RODAK_CHECK(req.response_body.find('\0') == std::string::npos);
    RODAK_CHECK(req.response_body.rfind("</html>") != std::string::npos);
}

RODAK_TEST("web files hides system folders from the root listing") {
    AccessFixture f;
    std::filesystem::create_directories(f.files.root + "/rodak-ota");
    std::filesystem::create_directories(f.files.root + "/rodakos/appearance");
    std::filesystem::create_directories(f.files.root + "/System Volume Information");
    const auto req = f.List(f.service.guard_.code());
    RODAK_CHECK(req.response_status.empty());
    RODAK_CHECK(req.response_body.find("\"Music\"") != std::string::npos);
    RODAK_CHECK(req.response_body.find("rodak-ota") == std::string::npos);
    RODAK_CHECK(req.response_body.find("\"rodakos\"") == std::string::npos);
    RODAK_CHECK(req.response_body.find("System Volume") == std::string::npos);
}

RODAK_TEST("web files refuses every operation inside a system folder") {
    AccessFixture f;
    std::filesystem::create_directories(f.files.root + "/rodak-ota");
    std::ofstream(f.files.root + "/rodak-ota/pending.bin") << "x";
    const std::string code = f.service.guard_.code();
    auto call = [&](esp_err_t (*handler)(httpd_req_t*), const std::string& query) {
        httpd_req_t req;
        req.user_ctx = &f.service;
        req.query = query;
        req.request_headers["X-Access-Code"] = code;
        handler(&req);
        return req.response_status;
    };
    using S = rodakos::WebFileSystemService;
    const std::string forbidden = "403 Forbidden";
    RODAK_CHECK_EQ(call(S::ListHandler, "path=%2Frodak-ota"), forbidden);
    RODAK_CHECK_EQ(call(S::ListHandler, "path=%2FRODAKOS%2Fappearance"), forbidden);
    RODAK_CHECK_EQ(call(S::DeleteHandler, "path=%2Frodak-ota"), forbidden);
    RODAK_CHECK_EQ(call(S::DeleteHandler, "path=%2Frodak-ota%2Fpending.bin"), forbidden);
    RODAK_CHECK_EQ(call(S::RenameHandler, "from=%2Frodak-ota&to=%2Fold"), forbidden);
    RODAK_CHECK_EQ(call(S::RenameHandler, "from=%2FMusic&to=%2Frodakos"), forbidden);
    RODAK_CHECK_EQ(call(S::MkdirHandler, "path=%2Frodakos"), forbidden);
    RODAK_CHECK_EQ(call(S::TicketHandler, "path=%2Frodak-ota%2Fpending.bin"), forbidden);
    RODAK_CHECK(std::filesystem::exists(f.files.root + "/rodak-ota/pending.bin"));
    // A user folder whose name only starts with a system name is still allowed.
    RODAK_CHECK(call(S::MkdirHandler, "path=%2Frodakos-backup").empty());
}
