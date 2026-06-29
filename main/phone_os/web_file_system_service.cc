#include "phone_os/web_file_system_service.h"

#include "rodakos_adapters/file_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "WebFiles";
constexpr uint16_t kWebFilePort = 8080;
constexpr size_t kIoBufferSize = 4096;
constexpr size_t kMaxUploadBytes = 100 * 1024 * 1024;

const char kIndexHtml[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>RodakOS Web Files</title>"
    "<style>"
    ":root{color-scheme:dark}body{margin:0;font:14px system-ui,-apple-system,Segoe UI,sans-serif;background:#101418;color:#eef3f7}"
    "main{max-width:760px;margin:0 auto;padding:20px 14px}h1{font-size:22px;margin:0 0 12px}"
    ".bar{display:flex;gap:8px;align-items:center;margin:10px 0;flex-wrap:wrap}.path{color:#9ed0ff;word-break:break-all}"
    "button,input{font:inherit;border-radius:8px;border:1px solid #34424d;padding:9px 10px;background:#18212a;color:#eef3f7}"
    "button{background:#22303b;cursor:pointer}button.primary{background:#2d7dff;border-color:#2d7dff}button.danger{background:#442029;border-color:#68313e}"
    "button:disabled{opacity:.55;cursor:not-allowed}input[type=file]{max-width:260px}"
    "table{width:100%;border-collapse:collapse;margin-top:10px}td,th{border-bottom:1px solid #26323c;padding:10px 6px;text-align:left}"
    "th{color:#aab8c3;font-weight:600}.name{cursor:pointer}.muted{color:#8d9aa5}.actions{white-space:nowrap;text-align:right}.actions button{margin-left:6px;padding:6px 8px}"
    "progress{width:100%;height:14px;margin-top:10px}#status{min-height:22px;margin-top:10px;color:#9ed0ff;white-space:pre-wrap}"
    "@media(max-width:560px){.hide-sm{display:none}.actions button{margin:3px 0 3px 5px}}"
    "</style></head><body><main>"
    "<h1>RodakOS Web Files</h1>"
    "<div class=\"bar\"><button id=\"up\">Up</button><button id=\"refresh\">Refresh</button><span class=\"path\" id=\"path\">/</span></div>"
    "<div class=\"bar\"><input id=\"file\" type=\"file\"><button class=\"primary\" id=\"upload\">Upload</button><button id=\"mkdir\">New folder</button></div>"
    "<progress id=\"bar\" value=\"0\" max=\"100\" hidden></progress><div id=\"status\"></div>"
    "<table><thead><tr><th>Name</th><th class=\"hide-sm\">Size</th><th class=\"actions\">Actions</th></tr></thead><tbody id=\"list\"></tbody></table>"
    "<script>"
    "let cwd='/';const $=id=>document.getElementById(id);"
    "function enc(v){return encodeURIComponent(v)}"
    "function join(a,b){if(a==='/'||!a)return '/'+b;return a+'/'+b}"
    "function parent(p){if(p==='/'||!p)return '/';let i=p.lastIndexOf('/');return i<=0?'/':p.slice(0,i)}"
    "function size(n){if(n<1024)return n+' B';if(n<1048576)return(n/1024).toFixed(1)+' KB';return(n/1048576).toFixed(1)+' MB'}"
    "async function textFetch(url,opt){let r=await fetch(url,opt);let t=await r.text();if(!r.ok)throw new Error(t||r.statusText);return t}"
    "async function load(p=cwd){cwd=p;$('path').textContent=cwd;$('status').textContent='Loading...';"
    "try{let r=await fetch('/api/list?path='+enc(cwd));if(!r.ok)throw new Error(await r.text());let data=await r.json();"
    "let rows=data.entries.map(e=>`<tr><td class=\"name\" data-name=\"${e.name}\" data-dir=\"${e.dir?1:0}\">${e.dir?'[DIR] ':'[FILE] '}${e.name}</td><td class=\"hide-sm muted\">${e.dir?'Folder':size(e.size)}</td><td class=\"actions\">${e.dir?'':`<button data-act=\"download\" data-name=\"${e.name}\">Download</button>`}<button data-act=\"rename\" data-name=\"${e.name}\">Rename</button><button class=\"danger\" data-act=\"delete\" data-name=\"${e.name}\">Delete</button></td></tr>`).join('');"
    "$('list').innerHTML=rows||'<tr><td colspan=\"3\" class=\"muted\">Empty folder</td></tr>';$('status').textContent='';"
    "}catch(e){$('status').textContent='List failed: '+e.message}}"
    "$('list').onclick=async ev=>{let t=ev.target;if(t.dataset.dir==='1')return load(join(cwd,t.dataset.name));"
    "if(t.dataset.act==='download')location.href='/api/download?path='+enc(join(cwd,t.dataset.name));"
    "if(t.dataset.act==='rename'){let old=join(cwd,t.dataset.name),name=prompt('Rename to',t.dataset.name);if(name)try{await textFetch('/api/rename?from='+enc(old)+'&to='+enc(join(cwd,name)),{method:'POST'});load()}catch(e){$('status').textContent=e.message}}"
    "if(t.dataset.act==='delete'){let p=join(cwd,t.dataset.name);if(confirm('Delete '+p+'?'))try{await textFetch('/api/delete?path='+enc(p),{method:'POST'});load()}catch(e){$('status').textContent=e.message}}};"
    "$('up').onclick=()=>load(parent(cwd));$('refresh').onclick=()=>load();"
    "$('mkdir').onclick=async()=>{let name=prompt('Folder name');if(!name)return;try{await textFetch('/api/mkdir?path='+enc(join(cwd,name)),{method:'POST'});load()}catch(e){$('status').textContent=e.message}};"
    "$('upload').onclick=()=>{let file=$('file').files[0];if(!file){$('status').textContent='Choose a file first.';return}"
    "let xhr=new XMLHttpRequest(),dest=join(cwd,file.name);xhr.open('POST','/api/upload?path='+enc(dest));xhr.setRequestHeader('Content-Type','application/octet-stream');"
    "$('upload').disabled=true;$('bar').hidden=false;$('bar').value=0;$('status').textContent='Uploading '+file.name+'...';"
    "xhr.upload.onprogress=e=>{if(e.lengthComputable)$('bar').value=Math.round(e.loaded*100/e.total)};"
    "xhr.onload=()=>{$('upload').disabled=false;$('bar').hidden=true;if(xhr.status>=200&&xhr.status<300){$('status').textContent='Uploaded '+dest;load()}else $('status').textContent=xhr.responseText||('Upload failed: '+xhr.status)};"
    "xhr.onerror=()=>{$('upload').disabled=false;$('bar').hidden=true;$('status').textContent='Upload failed: network error.'};xhr.send(file)};"
    "load('/');"
    "</script></main></body></html>";

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

void SendText(httpd_req_t* req, int status_code, const char* status, const char* text) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
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

WebFileSystemService::WebFileSystemService(FileService* file_service) : file_service_(file_service) {
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
        {"/*", HTTP_OPTIONS, OptionsHandler},
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
        state_.running = true;
        state_.url = "http://" + ip_address + ":" + std::to_string(kWebFilePort) + "/";
        state_.message = "Listening";
        xSemaphoreGive(mutex_);
    }
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
        state_.url.clear();
        state_.message = "Stopped";
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

void WebFileSystemService::CompleteUpload(const std::string& file_name, size_t bytes) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.busy = false;
        state_.active_bytes = 0;
        state_.last_file = file_name;
        state_.last_bytes = bytes;
        state_.message = "Last upload complete";
        xSemaphoreGive(mutex_);
    }
}

esp_err_t WebFileSystemService::IndexHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebFileSystemService::ListHandler(httpd_req_t* req) {
    auto* self = static_cast<WebFileSystemService*>(req->user_ctx);
    std::string path;
    if (self == nullptr || self->file_service_ == nullptr) {
        SendText(req, 500, "500 Internal Server Error", "File service unavailable");
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
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
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
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

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
    if (req->content_len <= 0 || req->content_len > kMaxUploadBytes) {
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

    const std::string full_path = std::string(self->file_service_->GetMountPoint()) + upload_path;
    if (!self->TryBeginWrite("Uploading", upload_path)) {
        SendText(req, 409, "409 Conflict", "Another write is already running");
        return ESP_FAIL;
    }

    FILE* fp = std::fopen(full_path.c_str(), "wb");
    if (fp == nullptr) {
        self->SetBusy(false);
        SendText(req, 500, "500 Internal Server Error", "Cannot open destination file");
        return ESP_FAIL;
    }

    uint8_t* buffer = AllocIoBuffer();
    if (buffer == nullptr) {
        std::fclose(fp);
        self->SetBusy(false);
        SendText(req, 500, "500 Internal Server Error", "No IO buffer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Receiving upload %s (%d bytes)", upload_path.c_str(), req->content_len);

    int remaining = req->content_len;
    size_t written_total = 0;
    bool failed = false;
    while (remaining > 0) {
        const int to_read = std::min<int>(remaining, static_cast<int>(kIoBufferSize));
        const int received = httpd_req_recv(req, reinterpret_cast<char*>(buffer), to_read);
        if (received <= 0) {
            failed = true;
            break;
        }

        const size_t written = std::fwrite(buffer, 1, static_cast<size_t>(received), fp);
        if (written != static_cast<size_t>(received)) {
            failed = true;
            break;
        }

        written_total += written;
        remaining -= received;
        self->AddActiveBytes(written);
    }

    heap_caps_free(buffer);
    std::fclose(fp);

    if (failed) {
        std::remove(full_path.c_str());
        self->SetBusy(false);
        SendText(req, 500, "500 Internal Server Error", "Upload failed");
        ESP_LOGE(TAG, "Upload failed: %s", upload_path.c_str());
        return ESP_FAIL;
    }

    self->CompleteUpload(upload_path, written_total);
    ESP_LOGI(TAG, "Upload complete: %s (%zu bytes)", upload_path.c_str(), written_total);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t WebFileSystemService::MkdirHandler(httpd_req_t* req) {
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
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
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
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t WebFileSystemService::DeleteHandler(httpd_req_t* req) {
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
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t WebFileSystemService::OptionsHandler(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    return httpd_resp_send(req, nullptr, 0);
}

}  // namespace rodakos
