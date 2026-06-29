#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp_err.h>
#include <esp_http_server.h>

namespace rodakos {

class FileService;

struct WebFileSystemServiceState {
    bool running = false;
    bool busy = false;
    std::string url;
    std::string token;
    std::string message;
    std::string last_file;
    size_t last_bytes = 0;
    size_t active_bytes = 0;
};

class WebFileSystemService {
public:
    explicit WebFileSystemService(FileService* file_service);
    ~WebFileSystemService();

    bool Start(const std::string& ip_address);
    void Stop();

    bool IsRunning() const;
    WebFileSystemServiceState GetState() const;

private:
    void SetMessage(const std::string& message);
    bool TryBeginWrite(const std::string& message, const std::string& file_name = "");
    void SetBusy(bool busy, const std::string& file_name = "");
    void AddActiveBytes(size_t bytes);
    void CompleteUpload(const std::string& file_name, size_t bytes);
    bool AuthenticateRequest(httpd_req_t* req) const;

    static esp_err_t IndexHandler(httpd_req_t* req);
    static esp_err_t ListHandler(httpd_req_t* req);
    static esp_err_t DownloadHandler(httpd_req_t* req);
    static esp_err_t UploadHandler(httpd_req_t* req);
    static esp_err_t MkdirHandler(httpd_req_t* req);
    static esp_err_t RenameHandler(httpd_req_t* req);
    static esp_err_t DeleteHandler(httpd_req_t* req);

    FileService* file_service_ = nullptr;
    httpd_handle_t server_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    WebFileSystemServiceState state_;
    std::string access_token_;
};

}  // namespace rodakos
