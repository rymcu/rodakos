#include "phone_os/web_file_system_service.h"

#include "rodakos_adapters/file_service.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <string>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <lwip/sockets.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "WebFiles";
constexpr uint16_t kWebFilePort = 8080;
constexpr size_t kIoBufferSize = 4096;
constexpr size_t kMaxUploadBytes = 100 * 1024 * 1024;
constexpr int kMaxUploadReceiveTimeouts = 90;

// The page lives in web_files_page.html (embedded at build time) so it is plain HTML/JS
// instead of a C++ string with two layers of escaping.
extern const char kWebFilesPageStart[] asm("_binary_web_files_page_html_start");
extern const char kWebFilesPageEnd[] asm("_binary_web_files_page_html_end");

bool IsHex(char ch) {
    return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
}

uint8_t HexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<uint8_t>(ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
        return static_cast<uint8_t>(ch - 'A' + 10);
    }
    return 0;
}

std::string UrlDecode(const char* text) {
    std::string decoded;
    if (text == nullptr) {
        return decoded;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '%' && IsHex(text[i + 1]) && IsHex(text[i + 2])) {
            decoded.push_back(static_cast<char>((HexValue(text[i + 1]) << 4) | HexValue(text[i + 2])));
            i += 2;
        } else if (text[i] == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(text[i]);
        }
    }
    return decoded;
}

std::string JsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char escaped[7] = {};
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned char>(ch));
                    out += escaped;
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

bool IsSafePath(const std::string& path, bool allow_root) {
    if (path.empty() || path.size() > 180) {
        return false;
    }
    if (path.find('\\') != std::string::npos || path.find(':') != std::string::npos) {
        return false;
    }
    if (path[0] != '/') {
        return false;
    }
    if (path == "/") {
        return allow_root;
    }
    if (path.find("//") != std::string::npos) {
        return false;
    }

    size_t start = 1;
    while (start < path.size()) {
        const size_t slash = path.find('/', start);
        const std::string segment = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        for (const char ch : segment) {
            const auto uch = static_cast<unsigned char>(ch);
            if (uch < 0x20 || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*') {
                return false;
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

std::string ParentPath(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

std::string FileName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::string HeaderFileName(const std::string& path) {
    std::string name = FileName(path);
    for (char& ch : name) {
        const auto uch = static_cast<unsigned char>(ch);
        if (uch < 0x20 || ch == '"' || ch == '\\' || ch == ';') {
            ch = '_';
        }
    }
    return name.empty() ? "download.bin" : name;
}

bool EnsureDirectory(FileService* file_service, const std::string& path) {
    if (file_service == nullptr || path.empty() || path == "/") {
        return true;
    }

    std::string current;
    size_t start = 1;
    while (start < path.size()) {
        const size_t slash = path.find('/', start);
        const std::string segment = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        current += "/" + segment;
        if (!file_service->Exists(current) && !file_service->CreateDirectory(current)) {
            return false;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

bool GetQueryValue(httpd_req_t* req, const char* key, std::string& value) {
    char query[384] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    char raw[220] = {};
    if (httpd_query_key_value(query, key, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    value = UrlDecode(raw);
    return true;
}

// Header names are case-insensitive in HTTP; esp_http_server matches them that way too.
bool GetHeaderValue(httpd_req_t* req, const char* name, std::string& value) {
    const size_t length = httpd_req_get_hdr_value_len(req, name);
    if (length == 0 || length > 64) return false;
    char buffer[65] = {};
    if (httpd_req_get_hdr_value_str(req, name, buffer, sizeof(buffer)) != ESP_OK) return false;
    value = buffer;
    return true;
}

void SendText(httpd_req_t* req, int status_code, const char* status, const char* text) {
    (void)status_code;
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    ESP_LOGW(TAG, "HTTP %d: %s", status_code, text);
}

uint8_t* AllocIoBuffer() {
    auto* buffer = static_cast<uint8_t*>(heap_caps_malloc(kIoBufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t*>(heap_caps_malloc(kIoBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return buffer;
}

}  // namespace

WebFileSystemService::WebFileSystemService(FileService* file_service)
    : file_service_(file_service),
      guard_([]() { return esp_random(); }, []() { return esp_timer_get_time() / 1000; }) {
    mutex_ = xSemaphoreCreateMutex();
    state_.message = "Stopped";
}

WebFileSystemService::~WebFileSystemService() {
    Stop();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool WebFileSystemService::Start(const std::string& ip_address) {
    if (ip_address.empty()) {
        SetMessage("WiFi is not connected");
        return false;
    }
    if (file_service_ == nullptr) {
        SetMessage("File service unavailable");
        return false;
    }
    if (!file_service_->IsMounted() && !file_service_->Init()) {
        SetMessage("SD card not mounted");
        return false;
    }
    if (server_ != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kWebFilePort;
    config.ctrl_port = 32768;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    // Eight routes today; the default limit is also 8, so leave room for the next one.
    config.max_uri_handlers = 12;
    config.stack_size = 6144;

    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        server_ = nullptr;
        SetMessage(std::string("HTTP start failed: ") + esp_err_to_name(ret));
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return false;
    }

    const struct {
        const char* uri;
        httpd_method_t method;
        esp_err_t (*handler)(httpd_req_t* req);
    } routes[] = {
        {"/", HTTP_GET, IndexHandler},
        {"/api/list", HTTP_GET, ListHandler},
        {"/api/download", HTTP_GET, DownloadHandler},
        {"/api/upload", HTTP_POST, UploadHandler},
        {"/api/mkdir", HTTP_POST, MkdirHandler},
        {"/api/rename", HTTP_POST, RenameHandler},
        {"/api/delete", HTTP_POST, DeleteHandler},
        {"/api/ticket", HTTP_POST, TicketHandler},
    };

    for (const auto& route : routes) {
        httpd_uri_t uri = {};
        uri.uri = route.uri;
        uri.method = route.method;
        uri.handler = route.handler;
        uri.user_ctx = this;
        ret = httpd_register_uri_handler(server_, &uri);
        if (ret != ESP_OK) {
            httpd_stop(server_);
            server_ = nullptr;
            SetMessage(std::string("HTTP handler failed: ") + esp_err_to_name(ret));
            ESP_LOGE(TAG, "Failed to register HTTP handler %s: %s", route.uri, esp_err_to_name(ret));
            return false;
        }
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        guard_.Reset();
        state_.running = true;
        state_.token = guard_.DisplayCode();
        state_.url = "http://" + ip_address + ":" + std::to_string(kWebFilePort);
        state_.message = "Listening";
        xSemaphoreGive(mutex_);
    }
    // The access code stays off the serial log.
    ESP_LOGI(TAG, "Web file system listening at %s", state_.url.c_str());
    return true;
}

void WebFileSystemService::Stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.running = false;
        state_.busy = false;
        state_.active_bytes = 0;
        state_.active_total_bytes = 0;
        state_.url.clear();
        state_.token.clear();
        state_.message = "Stopped";
        guard_.Clear();
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(TAG, "Web file system stopped");
}

bool WebFileSystemService::IsRunning() const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool running = state_.running;
    xSemaphoreGive(mutex_);
    return running;
}

WebFileSystemServiceState WebFileSystemService::GetState() const {
    WebFileSystemServiceState copy;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        copy = state_;
        xSemaphoreGive(mutex_);
    }
    return copy;
}

void WebFileSystemService::SetMessage(const std::string& message) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.message = message;
        xSemaphoreGive(mutex_);
    }
}

bool WebFileSystemService::TryBeginWrite(const std::string& message, const std::string& file_name) {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (state_.busy) {
        xSemaphoreGive(mutex_);
        return false;
    }
    state_.busy = true;
    state_.active_bytes = 0;
    state_.active_total_bytes = 0;
    state_.message = message;
    if (!file_name.empty()) {
        state_.last_file = file_name;
    }
    xSemaphoreGive(mutex_);
    return true;
}

void WebFileSystemService::SetBusy(bool busy, const std::string& file_name) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.busy = busy;
        state_.active_bytes = 0;
        state_.active_total_bytes = 0;
        if (!file_name.empty()) {
            state_.last_file = file_name;
        }
        xSemaphoreGive(mutex_);
    }
}

void WebFileSystemService::AddActiveBytes(size_t bytes) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.active_bytes += bytes;
        xSemaphoreGive(mutex_);
    }
}

void WebFileSystemService::SetActiveTotal(size_t bytes) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.active_total_bytes = bytes;
        xSemaphoreGive(mutex_);
    }
}

void WebFileSystemService::CompleteUpload(const std::string& file_name, size_t bytes) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.busy = false;
        state_.active_bytes = 0;
        state_.active_total_bytes = 0;
        state_.last_file = file_name;
        state_.last_bytes = bytes;
        state_.message = "Last upload complete";
        xSemaphoreGive(mutex_);
    }
}

uint32_t (*WebFileSystemService::peer_address_override_)(httpd_req_t*) = nullptr;

uint32_t WebFileSystemService::PeerAddress(httpd_req_t* req) {
    if (peer_address_override_ != nullptr) return peer_address_override_(req);
    const int fd = httpd_req_to_sockfd(req);
    sockaddr_storage address = {};
    socklen_t length = sizeof(address);
    if (fd < 0 || getpeername(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;
    if (address.ss_family == AF_INET) {
        return reinterpret_cast<sockaddr_in*>(&address)->sin_addr.s_addr;
    }
    if (address.ss_family == AF_INET6) {
        // IPv4-mapped or native IPv6: fold the last 32 bits into the key.
        const auto* in6 = reinterpret_cast<sockaddr_in6*>(&address);
        uint32_t tail = 0;
        std::memcpy(&tail, &in6->sin6_addr.s6_addr[12], sizeof(tail));
        return tail;
    }
    return 0;
}

bool WebFileSystemService::AuthenticateRequest(httpd_req_t* req) {
    std::string code;
    // The header keeps the code out of URLs, history and Referer; ?token= keeps old links working.
    if (!GetHeaderValue(req, "X-Access-Code", code)) GetQueryValue(req, "token", code);

    if (mutex_ == nullptr) {
        SendText(req, 503, "503 Service Unavailable", "Service unavailable");
        return false;
    }
    const uint32_t client = PeerAddress(req);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool running = state_.running;
    const WebAccessGuard::Result result =
        running ? guard_.Check(client, code) : WebAccessGuard::Result::kRejected;
    const int retry_after = guard_.RetryAfterSeconds(client);
    xSemaphoreGive(mutex_);

    switch (result) {
        case WebAccessGuard::Result::kAccepted:
            return true;
        case WebAccessGuard::Result::kLocked: {
            const std::string seconds = std::to_string(retry_after > 0 ? retry_after : 1);
            httpd_resp_set_hdr(req, "Retry-After", seconds.c_str());
            SendText(req, 429, "429 Too Many Requests", "Too many attempts");
            return false;
        }
        case WebAccessGuard::Result::kMissing:
            SendText(req, 401, "401 Unauthorized", "Missing access code");
            return false;
        case WebAccessGuard::Result::kRejected:
            SendText(req, 401, "401 Unauthorized", "Invalid access code");
            return false;
    }
    return false;
}

esp_err_t WebFileSystemService::IndexHandler(httpd_req_t* req) {
    // Public: the page holds no file data and asks for the access code itself.
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    // A legacy /?token= link must not leak the code to other sites through Referer.
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    // TEXT embedding appends a NUL terminator; do not send it.
    return httpd_resp_send(req, kWebFilesPageStart, kWebFilesPageEnd - kWebFilesPageStart - 1);
}

esp_err_t WebFileSystemService::ListHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFileSystemService*>(req->user_ctx);
    std::string path;
    if (self == nullptr || self->file_service_ == nullptr) {
        SendText(req, 500, "500 Internal Server Error", "File service unavailable");
        return ESP_FAIL;
    }
    if (!self->AuthenticateRequest(req)) {
        return ESP_FAIL;
    }
    if (!GetQueryValue(req, "path", path) || !IsSafePath(path, true)) {
        SendText(req, 400, "400 Bad Request", "Unsafe path");
        return ESP_FAIL;
    }
    if (!self->file_service_->IsMounted() && !self->file_service_->Init()) {
        SendText(req, 503, "503 Service Unavailable", "SD card not mounted");
        return ESP_FAIL;
    }

    std::vector<FileEntry> entries;
    if (!self->file_service_->ListDirectory(path, entries)) {
        SendText(req, 404, "404 Not Found", "Cannot list directory");
        return ESP_FAIL;
    }

    std::string json = "{\"path\":\"" + JsonEscape(path) + "\",\"entries\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"name\":\"" + JsonEscape(entries[i].name) + "\",";
        json += "\"dir\":";
        json += entries[i].is_directory ? "true" : "false";
        json += ",\"size\":";
        json += std::to_string(entries[i].size);
        json += ",\"mtime\":";
        json += std::to_string(entries[i].modified_time);
        json += "}";
    }
    json += "]}";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json.c_str(), json.size());
}

esp_err_t WebFileSystemService::DownloadHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFileSystemService*>(req->user_ctx);
    std::string path;
    if (self == nullptr || self->file_service_ == nullptr) {
        SendText(req, 500, "500 Internal Server Error", "File service unavailable");
        return ESP_FAIL;
    }
    if (!GetQueryValue(req, "path", path) || !IsSafePath(path, false)) {
        SendText(req, 400, "400 Bad Request", "Unsafe path");
        return ESP_FAIL;
    }
    // A plain browser download cannot send a header: it presents a one-time ticket instead.
    std::string ticket;
    bool authorized = false;
    if (GetQueryValue(req, "ticket", ticket)) {
        xSemaphoreTake(self->mutex_, portMAX_DELAY);
        authorized = self->state_.running && self->guard_.RedeemTicket(ticket, path);
        xSemaphoreGive(self->mutex_);
        if (!authorized) {
            SendText(req, 401, "401 Unauthorized", "Download link expired");
            return ESP_FAIL;
        }
    } else if (!self->AuthenticateRequest(req)) {
        return ESP_FAIL;
    }
    if (!self->file_service_->IsMounted() && !self->file_service_->Init()) {
        SendText(req, 503, "503 Service Unavailable", "SD card not mounted");
        return ESP_FAIL;
    }

    const std::string full_path = std::string(self->file_service_->GetMountPoint()) + path;
    FILE* fp = std::fopen(full_path.c_str(), "rb");
    if (fp == nullptr) {
        SendText(req, 404, "404 Not Found", "Cannot open file");
        return ESP_FAIL;
    }

    uint8_t* buffer = AllocIoBuffer();
    if (buffer == nullptr) {
        std::fclose(fp);
        SendText(req, 500, "500 Internal Server Error", "No IO buffer");
        return ESP_FAIL;
    }

    std::string disposition = "attachment; filename=\"" + HeaderFileName(path) + "\"";
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());

    esp_err_t ret = ESP_OK;
    while (true) {
        const size_t read = std::fread(buffer, 1, kIoBufferSize, fp);
        if (read > 0 && httpd_resp_send_chunk(req, reinterpret_cast<char*>(buffer), read) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
        if (read < kIoBufferSize) {
            if (std::ferror(fp)) {
                ret = ESP_FAIL;
            }
            break;
        }
    }

    heap_caps_free(buffer);
    std::fclose(fp);
    if (ret == ESP_OK) {
        ret = httpd_resp_send_chunk(req, nullptr, 0);
    }
    return ret;
}

esp_err_t WebFileSystemService::UploadHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFileSystemService*>(req->user_ctx);
    std::string upload_path;
    if (self == nullptr || self->file_service_ == nullptr) {
        SendText(req, 500, "500 Internal Server Error", "File service unavailable");
        return ESP_FAIL;
    }
    if (!self->AuthenticateRequest(req)) {
        return ESP_FAIL;
    }
    if (req->content_len <= 0 || static_cast<size_t>(req->content_len) > kMaxUploadBytes) {
        SendText(req, 413, "413 Payload Too Large", "Invalid upload size");
        return ESP_FAIL;
    }
    if (!GetQueryValue(req, "path", upload_path) || !IsSafePath(upload_path, false)) {
        SendText(req, 400, "400 Bad Request", "Unsafe destination path");
        return ESP_FAIL;
    }
    if (!self->file_service_->IsMounted() && !self->file_service_->Init()) {
        SendText(req, 503, "503 Service Unavailable", "SD card not mounted");
        return ESP_FAIL;
    }
    if (!EnsureDirectory(self->file_service_, ParentPath(upload_path))) {
        SendText(req, 500, "500 Internal Server Error", "Cannot create destination folder");
        return ESP_FAIL;
    }

    if (!self->TryBeginWrite("Uploading", upload_path)) {
        SendText(req, 409, "409 Conflict", "Another write is already running");
        return ESP_FAIL;
    }

    const std::string full_path = std::string(self->file_service_->GetMountPoint()) + upload_path;
    size_t written_total = 0;
    int lease_errno = 0;
    std::string failure_message = "Upload failed";
    const bool leased = self->file_service_->WithWriteLease(upload_path, [&]() {
        FILE* fp = std::fopen(full_path.c_str(), "wb");
        if (fp == nullptr) {
            failure_message = "Cannot open destination file";
            lease_errno = errno;
            return false;
        }

        uint8_t* buffer = AllocIoBuffer();
        if (buffer == nullptr) {
            std::fclose(fp);
            std::remove(full_path.c_str());
            failure_message = "No IO buffer";
            lease_errno = ENOMEM;
            return false;
        }

        ESP_LOGI(TAG, "Receiving upload %s (%d bytes)", upload_path.c_str(), req->content_len);
        self->SetActiveTotal(static_cast<size_t>(req->content_len));
        int remaining = req->content_len;
        bool failed = false;
        int operation_errno = 0;
        int timeout_count = 0;
        while (remaining > 0) {
            const int to_read = std::min<int>(remaining, static_cast<int>(kIoBufferSize));
            const int received = httpd_req_recv(req, reinterpret_cast<char*>(buffer), to_read);
            if (received <= 0) {
                if (received == HTTPD_SOCK_ERR_TIMEOUT && ++timeout_count <= kMaxUploadReceiveTimeouts) {
                    continue;
                }
                failure_message = "Upload receive failed";
                operation_errno = EIO;
                failed = true;
                break;
            }
            timeout_count = 0;

            const size_t written = std::fwrite(buffer, 1, static_cast<size_t>(received), fp);
            if (written != static_cast<size_t>(received) || std::ferror(fp) != 0) {
                failure_message = "Upload write failed";
                operation_errno = EIO;
                failed = true;
                break;
            }

            written_total += written;
            remaining -= received;
            self->AddActiveBytes(written);
        }

        heap_caps_free(buffer);
        if (!failed && std::fflush(fp) != 0) {
            failure_message = "Upload flush failed";
            operation_errno = errno;
            failed = true;
        }
        if (std::fclose(fp) != 0) {
            if (!failed) {
                failure_message = "Upload close failed";
                operation_errno = errno;
            }
            failed = true;
        }
        if (failed) {
            std::remove(full_path.c_str());
            lease_errno = operation_errno != 0 ? operation_errno : EIO;
            errno = lease_errno;
            return false;
        }
        return true;
    });
    if (!leased && lease_errno == 0) {
        lease_errno = errno;
    }

    self->SetBusy(false);
    if (!leased) {
        if (lease_errno == EBUSY) {
            SendText(req, 409, "409 Conflict", "Destination is busy");
        } else {
            SendText(req, 500, "500 Internal Server Error", failure_message.c_str());
        }
        ESP_LOGE(TAG, "Upload failed: %s (%s)", upload_path.c_str(), failure_message.c_str());
        return ESP_FAIL;
    }

    self->CompleteUpload(upload_path, written_total);
    ESP_LOGI(TAG, "Upload complete: %s (%zu bytes)", upload_path.c_str(), written_total);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t WebFileSystemService::MkdirHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFileSystemService*>(req->user_ctx);
    std::string path;
    if (self == nullptr || self->file_service_ == nullptr) {
        SendText(req, 500, "500 Internal Server Error", "File service unavailable");
        return ESP_FAIL;
    }
    if (!self->AuthenticateRequest(req)) {
        return ESP_FAIL;
    }
    if (!GetQueryValue(req, "path", path) || !IsSafePath(path, false)) {
        SendText(req, 400, "400 Bad Request", "Unsafe path");
        return ESP_FAIL;
    }
    if (!self->file_service_->IsMounted() && !self->file_service_->Init()) {
        SendText(req, 503, "503 Service Unavailable", "SD card not mounted");
        return ESP_FAIL;
    }
    if (!self->TryBeginWrite("Creating folder", path)) {
        SendText(req, 409, "409 Conflict", "Another write is already running");
        return ESP_FAIL;
    }
    const bool ok = EnsureDirectory(self->file_service_, path);
    self->SetBusy(false);
    if (!ok) {
        SendText(req, 500, "500 Internal Server Error", "Cannot create folder");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t WebFileSystemService::RenameHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFileSystemService*>(req->user_ctx);
    std::string from;
    std::string to;
    if (self == nullptr || self->file_service_ == nullptr) {
        SendText(req, 500, "500 Internal Server Error", "File service unavailable");
        return ESP_FAIL;
    }
    if (!self->AuthenticateRequest(req)) {
        return ESP_FAIL;
    }
    if (!GetQueryValue(req, "from", from) || !GetQueryValue(req, "to", to) ||
        !IsSafePath(from, false) || !IsSafePath(to, false)) {
        SendText(req, 400, "400 Bad Request", "Unsafe path");
        return ESP_FAIL;
    }
    if (!self->file_service_->IsMounted() && !self->file_service_->Init()) {
        SendText(req, 503, "503 Service Unavailable", "SD card not mounted");
        return ESP_FAIL;
    }
    if (!EnsureDirectory(self->file_service_, ParentPath(to))) {
        SendText(req, 500, "500 Internal Server Error", "Cannot create destination folder");
        return ESP_FAIL;
    }
    if (!self->TryBeginWrite("Renaming", from)) {
        SendText(req, 409, "409 Conflict", "Another write is already running");
        return ESP_FAIL;
    }
    const bool ok = self->file_service_->Rename(from, to);
    self->SetBusy(false, to);
    if (!ok) {
        SendText(req, 500, "500 Internal Server Error", "Rename failed");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t WebFileSystemService::TicketHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFileSystemService*>(req->user_ctx);
    std::string path;
    if (self == nullptr || !self->AuthenticateRequest(req)) {
        return ESP_FAIL;
    }
    if (!GetQueryValue(req, "path", path) || !IsSafePath(path, false)) {
        SendText(req, 400, "400 Bad Request", "Unsafe path");
        return ESP_FAIL;
    }
    xSemaphoreTake(self->mutex_, portMAX_DELAY);
    const std::string ticket = self->guard_.IssueTicket(path);
    xSemaphoreGive(self->mutex_);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, ticket.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebFileSystemService::DeleteHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFileSystemService*>(req->user_ctx);
    std::string path;
    if (self == nullptr || self->file_service_ == nullptr) {
        SendText(req, 500, "500 Internal Server Error", "File service unavailable");
        return ESP_FAIL;
    }
    if (!self->AuthenticateRequest(req)) {
        return ESP_FAIL;
    }
    if (!GetQueryValue(req, "path", path) || !IsSafePath(path, false)) {
        SendText(req, 400, "400 Bad Request", "Unsafe path");
        return ESP_FAIL;
    }
    if (!self->file_service_->IsMounted() && !self->file_service_->Init()) {
        SendText(req, 503, "503 Service Unavailable", "SD card not mounted");
        return ESP_FAIL;
    }
    if (!self->TryBeginWrite("Deleting", path)) {
        SendText(req, 409, "409 Conflict", "Another write is already running");
        return ESP_FAIL;
    }

    std::vector<FileEntry> ignored;
    const bool ok = self->file_service_->ListDirectory(path, ignored)
        ? self->file_service_->DeleteDirectory(path)
        : self->file_service_->DeleteFile(path);
    self->SetBusy(false, path);
    if (!ok) {
        SendText(req, 500, "500 Internal Server Error", "Delete failed");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "OK");
}

}  // namespace rodakos
