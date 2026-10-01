#include "phone_os/appearance_service.h"

#include "phone_os/device_cloud_config.h"
#include "rodakos_adapters/file_service.h"
#include "rodak_appearance_crypto.h"
#include "rodak_appearance_policy.h"
#include "rodak_sha256.h"
#include "settings.h"

#include "rodak_appearance_json.h"
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace rodakos {
namespace {
constexpr const char* TAG = "Appearance";
constexpr const char* kNamespace = "appearance";
constexpr const char* kStateKey = "state";
constexpr const char* kDirectory = "/rodakos/appearance";
constexpr size_t kJsonLimit = 8192;
constexpr uint32_t kBootBudgetMs = 1500;
constexpr size_t kAppearanceMinWorkerLargestBlock = 2048;
std::once_flag g_json_allocator;
void* AllocateJson(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
struct alignas(16) JsonAllocationHeader { size_t size; };
std::atomic<size_t> g_json_bytes{0};
void* AllocateJsonNode(size_t size) {
    constexpr size_t kJsonBudget = kAppearanceMaxJsonAllocationBytes;
    if (size > kJsonBudget - sizeof(JsonAllocationHeader)) return nullptr;
    const size_t actual = size + sizeof(JsonAllocationHeader);
    size_t current = g_json_bytes.load();
    do { if (current > kJsonBudget - actual) return nullptr; }
    while (!g_json_bytes.compare_exchange_weak(current, current + actual));
    auto* allocation = static_cast<JsonAllocationHeader*>(AllocateJson(actual));
    if (allocation == nullptr) { g_json_bytes.fetch_sub(actual); return nullptr; }
    allocation->size = actual;
    return allocation + 1;
}
void ReleaseJsonNode(void* data) {
    if (data == nullptr) return;
    auto* allocation = static_cast<JsonAllocationHeader*>(data) - 1;
    g_json_bytes.fetch_sub(allocation->size); heap_caps_free(allocation);
}
int64_t NowMs() { return esp_timer_get_time() / 1000; }
std::string Text(const cJSON* object, const char* name) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : "";
}
bool Integer(const cJSON* object, const char* name, uint64_t min, uint64_t max, uint64_t& out) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble || item->valuedouble < static_cast<double>(min) || item->valuedouble > static_cast<double>(max)) return false;
    out = static_cast<uint64_t>(item->valuedouble); return true;
}
std::string Encode(cJSON* object) {
    char* text = cJSON_PrintUnformatted(object);
    if (text == nullptr) return {};
    std::string encoded(text); cJSON_free(text); return encoded;
}
std::string Origin(const std::string& value) {
    const size_t scheme = value.find("://");
    if ((value.rfind("http://", 0) != 0 && value.rfind("https://", 0) != 0) || scheme == std::string::npos) return {};
    const size_t slash = value.find('/', scheme + 3);
    const std::string result = value.substr(0, slash);
    return result.find_first_of("\r\n@?#") == std::string::npos ? result : std::string();
}
std::string UrlEncode(const std::string& text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : text) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.') out.push_back(static_cast<char>(ch));
        else { out.push_back('%'); out.push_back(kHex[ch >> 4]); out.push_back(kHex[ch & 15]); }
    }
    return out;
}
bool HttpJson(const std::string& url, const std::string& token, const std::string& body, std::string& response, int* status = nullptr) {
    esp_http_client_config_t config = {};
    config.url = url.c_str(); config.method = body.empty() ? HTTP_METHOD_GET : HTTP_METHOD_POST;
    config.timeout_ms = 10000; config.buffer_size = 1024; config.buffer_size_tx = 1024;
    config.disable_auto_redirect = true; config.crt_bundle_attach = esp_crt_bundle_attach; config.user_agent = "RodakOS/appearance-v1";
    auto client = esp_http_client_init(&config);
    if (client == nullptr) return false;
    const std::string authorization = "Bearer " + token;
    esp_http_client_set_header(client, "Authorization", authorization.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/json");
    bool ok = esp_http_client_open(client, static_cast<int>(body.size())) == ESP_OK;
    if (ok && !body.empty()) ok = esp_http_client_write(client, body.data(), static_cast<int>(body.size())) == static_cast<int>(body.size());
    if (ok) ok = esp_http_client_fetch_headers(client) >= 0;
    if (status != nullptr) *status = esp_http_client_get_status_code(client);
    ok = ok && esp_http_client_get_status_code(client) == 200;
    std::shared_ptr<char> buffer(static_cast<char*>(AllocateJson(kJsonLimit + 1)), heap_caps_free);
    const int read = ok && buffer ? esp_http_client_read_response(client, buffer.get(), kJsonLimit + 1) : -1;
    ok = ok && read > 0 && static_cast<size_t>(read) <= kJsonLimit;
    esp_http_client_close(client); esp_http_client_cleanup(client);
    if (!ok) return false;
    cJSON* root = cJSON_ParseWithLength(buffer.get(), static_cast<size_t>(read));
    const cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "code");
    cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    ok = cJSON_IsNumber(code) && code->valueint == 200 && cJSON_IsObject(data);
    if (ok) response = Encode(data);
    cJSON_Delete(root); return ok && !response.empty();
}
bool FreshJson(DeviceCloudConfigService& cloud, DeviceCloudConfig& config,
               const std::string& path, const std::string& body, std::string& response,
               const std::function<bool()>& can_continue, int* final_status = nullptr) {
    if (final_status != nullptr) *final_status = 0;
    const std::string origin = Origin(config.mqtt_http_base_url), device_key = config.mqtt_device_key;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        if (can_continue && !can_continue()) return false;
        int status = 0;
        const bool ok = HttpJson(origin + path, config.aiot_access_token, body, response, &status);
        if (final_status != nullptr) *final_status = status;
        if (ok) return true;
        if (!ShouldRetryAppearanceAuthentication(status, attempt)) return false;
        cloud.InvalidateAccessTokenFreshness(config.aiot_access_token);
        if (!cloud.PrepareVoiceConfig(config, can_continue) || Origin(config.mqtt_http_base_url) != origin || config.mqtt_device_key != device_key) return false;
    }
    return false;
}
bool TransientHttp(int status) { return status == 0 || status == 408 || status == 429 || status >= 500; }
bool ReadSmallFile(const std::string& path, size_t maximum, std::string& out) {
    FILE* file = std::fopen(path.c_str(), "rb"); if (file == nullptr) return false;
    std::vector<char> bytes(maximum + 1);
    const size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    const bool ok = read <= maximum && std::feof(file) && !std::ferror(file);
    const bool closed = std::fclose(file) == 0;
    if (ok && closed) out.assign(bytes.data(), read);
    return ok && closed;
}
bool WriteDurableFile(const std::string& path, const std::string& encoded) {
    FILE* file = std::fopen(path.c_str(), "wb"); if (file == nullptr) return false;
    const bool ok = std::fwrite(encoded.data(), 1, encoded.size(), file) == encoded.size() && std::fflush(file) == 0 && fsync(fileno(file)) == 0;
    return std::fclose(file) == 0 && ok;
}
uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
std::shared_ptr<AppearanceBootAssets> BuiltinAssets(uint32_t revision) {
    auto assets = std::make_shared<AppearanceBootAssets>(); assets->revision = revision;
    assets->metadata.animation_kind = "builtin"; assets->metadata.animation_template = "letters";
    assets->metadata.duration_ms = 2500; assets->metadata.background = 0; assets->metadata.color = 0xeaf7ff;
    assets->metadata.theme_preset = "dark"; assets->metadata.theme_primary = 0x79cbff;
    return assets;
}
struct RangeHeaders {
    std::string content_range;
    bool invalid = false;
    bool seen = false;
};
esp_err_t RangeHeaderEvent(esp_http_client_event_t* event) {
    if (event->event_id != HTTP_EVENT_ON_HEADER || event->user_data == nullptr || event->header_key == nullptr || event->header_value == nullptr) return ESP_OK;
    std::string name(event->header_key);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (name != "content-range") return ESP_OK;
    auto* headers = static_cast<RangeHeaders*>(event->user_data);
    if (headers->seen || std::strlen(event->header_value) > 96) headers->invalid = true;
    else headers->content_range = event->header_value;
    headers->seen = true;
    return ESP_OK;
}
}  // namespace

AppearanceService::AppearanceService(DeviceCloudConfigService& cloud, FileService* files) : cloud_(cloud), files_(files) {
    std::call_once(g_json_allocator, []() { AppearanceJsonSetAllocator(AllocateJsonNode, ReleaseJsonNode); });
    boot_ready_ = xSemaphoreCreateBinaryStatic(&boot_ready_storage_);
    LoadState();
}
AppearanceService::~AppearanceService() {
    boot_abandoned_.store(true);
    while (boot_running_.load() || worker_running_.load()) vTaskDelay(pdMS_TO_TICKS(10));
}
bool AppearanceService::LoadState() {
    Settings settings(kNamespace, false);
    std::string encoded;
    const auto status = settings.ReadString(kStateKey, encoded, 6144);
    if (status == SettingsStringReadStatus::kNotFound) return true;
    if (status != SettingsStringReadStatus::kOk) { status_ = "fallback"; fallback_reason_ = "state_unavailable"; return false; }
    cJSON* root = cJSON_Parse(encoded.c_str());
    uint64_t schema = 0;
    if (!cJSON_IsObject(root) || !Integer(root, "schemaVersion", 1, 1, schema)) {
        cJSON_Delete(root); status_ = "fallback"; fallback_reason_ = "state_invalid"; return false;
    }
    const auto decode = [&](const char* name, StoredRelease& release) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, name);
        if (value == nullptr || cJSON_IsNull(value)) return true;
        uint64_t revision = 0, size = 0;
        if (!cJSON_IsObject(value) || !Integer(value, "revision", 1, 0xffffffff, revision) ||
            !Integer(value, "size", 0, kAppearanceMaxPackageBytes, size)) return false;
        release.deployment_id = Text(value, "deploymentId"); release.release_id = Text(value, "releaseId");
        release.key_id = Text(value, "keyId"); release.sha256 = Text(value, "sha256"); release.mode = Text(value, "mode"); release.slot = Text(value, "slot");
        release.revision = static_cast<uint32_t>(revision); release.size = static_cast<size_t>(size);
        return IsAppearanceIdentifier(release.deployment_id) && IsAppearanceSha256(release.key_id) &&
            ((release.mode == "builtin" && size == 0 && release.slot.empty()) ||
             (release.mode == "custom" && size >= 16 && IsAppearanceSha256(release.sha256) && (release.slot == "a" || release.slot == "b")));
    };
    const bool ok = decode("active", active_) && decode("pending", pending_) && decode("previous", previous_);
    uint64_t trial = 0, rejected = 0;
    if (Integer(root, "trialRevision", 0, 0xffffffff, trial)) trial_revision_ = static_cast<uint32_t>(trial);
    if (Integer(root, "rejectedRevision", 0, 0xffffffff, rejected)) rejected_revision_ = static_cast<uint32_t>(rejected);
    trusted_key_id_ = Text(root, "publisherKeyId"); trusted_public_key_ = Text(root, "publisherPublicKey"); trusted_origin_ = Text(root, "publisherOrigin");
    std::string verified_key;
    if (!trusted_key_id_.empty() && (!AppearancePublicKeyId(trusted_public_key_, verified_key) || verified_key != trusted_key_id_ || Origin(trusted_origin_) != trusted_origin_)) {
        trusted_key_id_.clear(); trusted_public_key_.clear(); trusted_origin_.clear();
    }
    const cJSON* local = cJSON_GetObjectItemCaseSensitive(root, "localTheme");
    uint64_t primary = 0;
    if (cJSON_IsObject(local) && IsAppearancePreset(Text(local, "preset")) && Integer(local, "primary", 0, 0xffffff, primary)) {
        local_theme_ = true; local_preset_ = Text(local, "preset"); local_primary_ = static_cast<uint32_t>(primary);
    }
    latest_revision_ = std::max(active_.revision, pending_.revision);
    uint64_t latest = 0;
    if (Integer(root, "latestRevision", latest_revision_, 0xffffffff, latest)) latest_revision_ = static_cast<uint32_t>(latest);
    cJSON_Delete(root);
    if (!ok) { active_ = {}; pending_ = {}; status_ = "fallback"; fallback_reason_ = "state_invalid"; return false; }
    status_ = pending_.revision != 0 ? "pending_reboot" : active_.revision != 0 ? "applied" : "idle";
    AppearanceRevisionState revisions{active_.revision, pending_.revision, previous_.revision, trial_revision_, rejected_revision_};
    if (RecoverAppearanceTrial(revisions)) {
        active_ = previous_; pending_ = {}; trial_revision_ = 0; rejected_revision_ = revisions.rejected;
        status_ = "fallback"; fallback_reason_ = "previous_boot_unconfirmed";
        if (!SaveStateLocked()) error_ = "trial_recovery_commit_failed";
    }
    return true;
}
bool AppearanceService::SaveStateLocked() {
    cJSON* root = cJSON_CreateObject();
    bool complete = root != nullptr;
    const auto number = [&](cJSON* object, const char* name, double value) { complete = cJSON_AddNumberToObject(object, name, value) != nullptr && complete; };
    const auto text = [&](cJSON* object, const char* name, const std::string& value) { complete = cJSON_AddStringToObject(object, name, value.c_str()) != nullptr && complete; };
    const auto item = [&](cJSON* object, const char* name, cJSON* value) {
        if (value == nullptr || !cJSON_AddItemToObject(object, name, value)) { cJSON_Delete(value); complete = false; }
    };
    number(root, "schemaVersion", 1);
    number(root, "latestRevision", latest_revision_);
    number(root, "trialRevision", trial_revision_);
    number(root, "rejectedRevision", rejected_revision_);
    const auto add = [&](const char* name, const StoredRelease& release) {
        if (release.revision == 0) return;
        cJSON* value = cJSON_CreateObject();
        complete = value != nullptr && complete;
        number(value, "revision", release.revision); number(value, "size", release.size);
        text(value, "deploymentId", release.deployment_id); text(value, "releaseId", release.release_id);
        text(value, "keyId", release.key_id); text(value, "sha256", release.sha256);
        text(value, "mode", release.mode); text(value, "slot", release.slot);
        item(root, name, value);
    };
    add("active", active_); add("pending", pending_); add("previous", previous_);
    text(root, "publisherKeyId", trusted_key_id_); text(root, "publisherPublicKey", trusted_public_key_);
    text(root, "publisherOrigin", trusted_origin_);
    if (local_theme_) {
        cJSON* theme = cJSON_CreateObject(); complete = theme != nullptr && complete;
        text(theme, "preset", local_preset_); number(theme, "primary", local_primary_); item(root, "localTheme", theme);
    }
    const std::string encoded = complete ? Encode(root) : std::string(); cJSON_Delete(root);
    if (encoded.empty() || encoded.size() > 6144) return false;
    Settings settings(kNamespace, true);
    return settings.SetString(kStateKey, encoded) && settings.Commit();
}
bool AppearanceService::BeginBootLoad() {
    StoredRelease target;
    { std::lock_guard<std::mutex> lock(mutex_); target = pending_.revision != 0 ? pending_ : active_; }
    boot_started_ms_ = NowMs(); boot_abandoned_.store(false);
    if (target.mode != "custom") {
        std::lock_guard<std::mutex> lock(mutex_); boot_candidate_ = target.revision != 0; selected_revision_ = target.revision;
        if (target.mode == "builtin" && target.revision != 0) {
            boot_assets_ = BuiltinAssets(target.revision);
        }
        if (pending_.revision != 0) {
            previous_ = active_; trial_revision_ = pending_.revision;
            if (!SaveStateLocked()) { boot_candidate_ = false; trial_revision_ = 0; status_ = "fallback"; fallback_reason_ = "trial_commit_failed"; }
        }
        return false;
    }
    bool expected = false;
    if (!boot_running_.compare_exchange_strong(expected, true)) return true;
    if (xTaskCreate(BootTask, "appearance_boot", 8192, this, 1, nullptr) != pdPASS) {
        boot_running_.store(false); SetStatus("fallback", "boot_task_unavailable"); return false;
    }
    return true;
}
void AppearanceService::BootTask(void* arg) {
    auto* self = static_cast<AppearanceService*>(arg);
    self->LoadBootAssets(); self->boot_running_.store(false); xSemaphoreGive(self->boot_ready_); vTaskDelete(nullptr);
}
void AppearanceService::LoadBootAssets() {
    StoredRelease target, active;
    bool trying_pending = false;
    { std::lock_guard<std::mutex> lock(mutex_); target = pending_.revision != 0 ? pending_ : active_; active = active_; trying_pending = pending_.revision != 0; }
    auto assets = ReadPackage(target);
    if (!assets && trying_pending && !boot_abandoned_.load() && NowMs() - boot_started_ms_ <= kBootBudgetMs) {
        { std::lock_guard<std::mutex> lock(mutex_);
          rejected_revision_ = std::max(rejected_revision_, target.revision); pending_ = {}; trial_revision_ = 0;
          fallback_reason_ = "pending_package_invalid"; status_ = "fallback";
          if (!SaveStateLocked()) error_ = "pending_rejection_commit_failed";
        }
        if (active.mode == "custom" && files_ != nullptr && files_->IsMounted()) assets = ReadPackage(active);
        else if (active.mode == "builtin") assets = BuiltinAssets(active.revision);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    load_ms_ = static_cast<uint32_t>(NowMs() - boot_started_ms_);
    if (boot_abandoned_.load() || load_ms_ > kBootBudgetMs) { fallback_reason_ = "load_timeout"; status_ = "fallback"; return; }
    if (!assets) { if (fallback_reason_.empty()) fallback_reason_ = "package_unavailable"; status_ = "fallback"; return; }
    selected_revision_ = assets->revision;
    boot_playing_.store(assets->metadata.animation_kind != "builtin" && !assets->buffers.empty());
    boot_assets_ = std::move(assets); boot_candidate_ = true;
}
std::shared_ptr<AppearanceBootAssets> AppearanceService::WaitBootAssets(uint32_t budget_ms) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& target = pending_.revision != 0 ? pending_ : active_;
        if (target.mode != "custom") return boot_assets_;
    }
    const int64_t remaining = static_cast<int64_t>(budget_ms) - (NowMs() - boot_started_ms_);
    if (boot_running_.load() && remaining > 0) xSemaphoreTake(boot_ready_, pdMS_TO_TICKS(remaining));
    std::lock_guard<std::mutex> lock(mutex_);
    if (boot_running_.load() || NowMs() - boot_started_ms_ > budget_ms) {
        boot_abandoned_.store(true); boot_candidate_ = false; boot_playing_.store(false); boot_assets_.reset(); status_ = "fallback"; fallback_reason_ = "load_timeout";
        load_ms_ = static_cast<uint32_t>(NowMs() - boot_started_ms_);
        ESP_LOGW(TAG, "Boot assets fallback: reason=load_timeout load_ms=%u budget_ms=%u", load_ms_, budget_ms);
        return nullptr;
    }
    if (boot_assets_ && pending_.revision == boot_assets_->revision && trial_revision_ == 0) {
        AppearanceRevisionState revisions{active_.revision, pending_.revision, previous_.revision, trial_revision_, rejected_revision_};
        if (!BeginAppearanceTrial(revisions)) { boot_candidate_ = false; boot_playing_.store(false); boot_assets_.reset(); status_ = "fallback"; fallback_reason_ = "trial_invalid"; return nullptr; }
        const StoredRelease old_previous = previous_; previous_ = active_; trial_revision_ = revisions.trial;
        if (!SaveStateLocked()) {
            previous_ = old_previous; trial_revision_ = 0; boot_candidate_ = false; boot_playing_.store(false); boot_assets_.reset(); status_ = "fallback"; fallback_reason_ = "trial_commit_failed"; return nullptr;
        }
    }
    return boot_assets_;
}
std::string AppearanceService::SdPath(const std::string& relative) const { return std::string(files_->GetMountPoint()) + relative; }
std::shared_ptr<AppearanceBootAssets> AppearanceService::ReadPackage(const StoredRelease& release) {
    std::shared_ptr<AppearanceBootAssets> result;
    if (files_ == nullptr || release.mode != "custom") return nullptr;
    const bool ok = files_->WithIoLock([&]() {
        if ((!files_->IsMounted() && !files_->Init()) || boot_abandoned_.load() || NowMs() - boot_started_ms_ > kBootBudgetMs) return false;
        const std::string base = std::string(kDirectory) + "/" + release.slot;
        std::string error;
        {
            DeviceCloudConfig config; cloud_.Load(config);
            std::string key_id, public_key;
            { std::lock_guard<std::mutex> lock(mutex_); key_id = trusted_key_id_; public_key = trusted_public_key_; }
            if (release.key_id != key_id || key_id.empty()) return false;
            std::string manifest;
            AppearanceSignedRelease signed_release;
            if (!ReadSmallFile(SdPath(base + ".manifest"), 8192, manifest) ||
                !VerifyAppearanceManifest(manifest, public_key, key_id, config.mqtt_device_key, signed_release, error) ||
                signed_release.revision != release.revision || signed_release.deployment_id != release.deployment_id ||
                signed_release.package_size != release.size || signed_release.package_sha256 != release.sha256) return false;
        }
        FILE* file = std::fopen(SdPath(base + ".rap").c_str(), "rb"); if (file == nullptr) return false;
        const auto close = [&]() { std::fclose(file); };
        std::array<uint8_t, 16> header = {};
        if (std::fread(header.data(), 1, header.size(), file) != header.size()) { close(); return false; }
        const size_t json_size = Le32(header.data() + 12);
        if (json_size == 0 || json_size > kAppearanceMaxMetadataBytes) { close(); return false; }
        std::shared_ptr<char> metadata(static_cast<char*>(heap_caps_malloc(json_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), heap_caps_free);
        auto assets = std::make_shared<AppearanceBootAssets>();
        if (!metadata || std::fread(metadata.get(), 1, json_size, file) != json_size ||
            !DecodeAppearanceMetadata(header.data(), metadata.get(), json_size, release.size, assets->metadata, error)) { close(); return false; }
        Sha256 hash;
        bool valid = hash.Start() && hash.Update(header.data(), header.size()) && hash.Update(metadata.get(), json_size);
        metadata.reset(); size_t total = header.size() + json_size;
        std::array<uint8_t, 160> row = {};
        size_t decoded_bytes = 0;
        for (const auto& resource : assets->metadata.resources) {
            if (!valid) break;
            const size_t decoded_size = resource.format == AppearancePixelFormat::kA4 ? static_cast<size_t>(resource.width) * resource.height : resource.length;
            auto* bytes = static_cast<uint8_t*>(heap_caps_malloc(decoded_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (bytes == nullptr) { valid = false; break; }
            std::shared_ptr<uint8_t> decoded(bytes, heap_caps_free);
            decoded_bytes += decoded_size;
            if (valid && resource.format == AppearancePixelFormat::kA4) {
                const size_t stride = (resource.width + 1u) / 2u;
                for (size_t y = 0; valid && y < resource.height; ++y) {
                    valid = std::fread(row.data(), 1, stride, file) == stride && hash.Update(row.data(), stride);
                    total += stride;
                    for (size_t x = 0; valid && x < resource.width; ++x) {
                        const unsigned value = (x % 2 == 0) ? row[x / 2] >> 4 : row[x / 2] & 15;
                        bytes[y * resource.width + x] = static_cast<uint8_t>(value * 17);
                    }
                }
            } else if (valid) {
                size_t offset = 0;
                while (valid && offset < resource.length) {
                    const size_t length = std::min<size_t>(4096, resource.length - offset);
                    valid = std::fread(bytes + offset, 1, length, file) == length && hash.Update(bytes + offset, length);
                    offset += length; total += length;
                    valid = valid && !boot_abandoned_.load() && NowMs() - boot_started_ms_ <= kBootBudgetMs;
                }
            }
            valid = valid && !boot_abandoned_.load() && NowMs() - boot_started_ms_ <= kBootBudgetMs;
            assets->buffers.push_back(std::move(decoded));
        }
        std::array<unsigned char, 32> digest = {};
        valid = valid && total == release.size && std::fread(row.data(), 1, 1, file) == 0 &&
            std::feof(file) && !std::ferror(file) && hash.Finish(digest) && Sha256ToHex(digest) == release.sha256;
        close();
        if (!valid) return false;
        ESP_LOGI(TAG, "Boot assets ready: revision=%u decoded_bytes=%u psram_free=%u load_ms=%u", release.revision,
                 static_cast<unsigned>(decoded_bytes), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(NowMs() - boot_started_ms_));
        assets->revision = release.revision; result = std::move(assets); return true;
    });
    return ok ? result : nullptr;
}
void AppearanceService::RecordAnimationMs(uint32_t duration_ms) { std::lock_guard<std::mutex> lock(mutex_); animation_ms_ = duration_ms; }
void AppearanceService::ReleaseBootAssets() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (boot_assets_) {
        for (size_t i = 0; i < boot_assets_->metadata.resources.size(); ++i) {
            if (boot_assets_->metadata.resources[i].id != boot_assets_->metadata.wallpaper_resource_id) boot_assets_->buffers[i].reset();
        }
    }
    boot_playing_.store(false);
}
std::shared_ptr<AppearanceBootAssets> AppearanceService::GetBootAssets() const { std::lock_guard<std::mutex> lock(mutex_); return boot_assets_; }
bool AppearanceService::ConfirmBootHealthy() {
    bool ok = true;
    { std::lock_guard<std::mutex> lock(mutex_);
      if (!boot_candidate_ || boot_abandoned_.load()) return false;
      if (pending_.revision != 0) {
          AppearanceRevisionState revisions{active_.revision, pending_.revision, previous_.revision, trial_revision_, rejected_revision_};
          if (!ConfirmAppearanceTrial(revisions, selected_revision_)) return false;
          const StoredRelease old = active_; const StoredRelease next = pending_;
          const bool old_local = local_theme_; const uint32_t old_trial = trial_revision_;
          active_ = pending_; pending_ = {}; local_theme_ = false; trial_revision_ = 0;
          ok = SaveStateLocked();
          if (!ok) { active_ = old; pending_ = next; local_theme_ = old_local; trial_revision_ = old_trial; status_ = "failed"; error_ = "state_commit_failed"; }
      }
      if (ok) {
          status_ = selected_revision_ < latest_revision_ && !fallback_reason_.empty() ? "fallback" : "applied";
          progress_ = 100;
          if (status_ == "applied") { fallback_reason_.clear(); error_.clear(); }
      }
    }
    if (ok) {
        uint32_t revision = 0;
        { std::lock_guard<std::mutex> lock(mutex_); revision = active_.revision; }
        ESP_LOGI(TAG, "Appearance boot confirmed: active_revision=%u", revision);
    }
    NotifyState(); return ok;
}
void AppearanceService::RejectBootCandidate(const std::string& reason) {
    { std::lock_guard<std::mutex> lock(mutex_);
      boot_candidate_ = false; boot_abandoned_.store(true); boot_playing_.store(false); boot_assets_.reset();
      if (trial_revision_ != 0) {
          AppearanceRevisionState revisions{active_.revision, pending_.revision, previous_.revision, trial_revision_, rejected_revision_};
          RecoverAppearanceTrial(revisions); active_ = previous_; pending_ = {}; trial_revision_ = 0; rejected_revision_ = revisions.rejected;
          if (!SaveStateLocked()) error_ = "trial_rejection_commit_failed";
      }
      status_ = "fallback"; fallback_reason_ = reason;
    }
    NotifyState();
}
void AppearanceService::SetBusyGate(std::function<bool()> gate) { std::lock_guard<std::mutex> lock(mutex_); busy_gate_ = std::move(gate); }
void AppearanceService::SetStatePublisher(std::function<void()> publisher) { std::lock_guard<std::mutex> lock(mutex_); state_publisher_ = std::move(publisher); }
void AppearanceService::NotifyState() {
    std::function<void()> publisher;
    { std::lock_guard<std::mutex> lock(mutex_); publisher = state_publisher_; }
    if (publisher) publisher();
}
void AppearanceService::SetStatus(const std::string& status, const std::string& error, int progress) {
    { std::lock_guard<std::mutex> lock(mutex_); status_ = status; error_ = error; if (progress >= 0) progress_ = progress; }
    NotifyState();
}
void AppearanceService::DeferDownload(const std::string& error) {
    { std::lock_guard<std::mutex> lock(mutex_);
      const uint32_t delay = AppearanceDownloadRetryDelay(++download_failures_);
      if (delay == 0) { status_ = "failed"; error_ = error + "_retry_exhausted"; }
      else { status_ = "waiting_device"; error_ = error; download_retry_at_ms_ = NowMs() + delay; }
    }
    NotifyState();
}
void AppearanceService::OnNetworkReady() {
    DeviceCloudConfig config; cloud_.Load(config);
    { std::lock_guard<std::mutex> lock(mutex_);
      device_key_ = config.mqtt_device_key;
      if (publisher_.key_id.empty()) publisher_requested_.store(true);
    }
    ScheduleWorker();
}
void AppearanceService::RequestPublisher() { publisher_requested_.store(true); ScheduleWorker(); }
AppearancePublisherState AppearanceService::GetPublisherState() const { std::lock_guard<std::mutex> lock(mutex_); return publisher_; }
bool AppearanceService::ConfirmPublisher(const std::string& key_id) {
    bool ok = false;
    { std::lock_guard<std::mutex> lock(mutex_);
      if (publisher_.loading || key_id.empty() || key_id != publisher_.key_id || publisher_origin_.empty()) return false;
      const auto old_key = trusted_key_id_, old_pem = trusted_public_key_, old_origin = trusted_origin_;
      const StoredRelease old_active = active_, old_pending = pending_, old_previous = previous_;
      const uint32_t old_trial = trial_revision_, old_rejected = rejected_revision_, old_latest = latest_revision_;
      const std::string old_desired = desired_, old_status = status_, old_error = error_, old_fallback = fallback_reason_;
      const int old_progress = progress_;
      if (ShouldResetAppearanceEpoch(old_key, old_origin, key_id, publisher_origin_)) {
          active_ = {}; pending_ = {}; previous_ = {}; trial_revision_ = 0; rejected_revision_ = 0; latest_revision_ = 0;
          cJSON* desired = cJSON_Parse(desired_.c_str()); uint64_t revision = 0;
          const bool matches = Text(desired, "keyId") == key_id && Integer(desired, "revision", 1, 0xffffffff, revision);
          cJSON_Delete(desired);
          if (matches) { latest_revision_ = static_cast<uint32_t>(revision); status_ = "waiting_device"; }
          else { desired_.clear(); status_ = "idle"; }
          error_.clear(); fallback_reason_.clear(); progress_ = 0;
      }
      trusted_key_id_ = key_id; trusted_public_key_ = publisher_.public_key; trusted_origin_ = publisher_origin_;
      ok = SaveStateLocked();
      if (!ok) {
          trusted_key_id_ = old_key; trusted_public_key_ = old_pem; trusted_origin_ = old_origin;
          active_ = old_active; pending_ = old_pending; previous_ = old_previous;
          trial_revision_ = old_trial; rejected_revision_ = old_rejected; latest_revision_ = old_latest;
          desired_ = old_desired; status_ = old_status; error_ = old_error; fallback_reason_ = old_fallback; progress_ = old_progress;
      }
      publisher_.trusted = ok || (publisher_.key_id == trusted_key_id_ && publisher_origin_ == trusted_origin_);
    }
    NotifyState(); if (ok) ScheduleWorker(); return ok;
}
bool AppearanceService::ForgetPublisher() {
    bool ok = false;
    { std::lock_guard<std::mutex> lock(mutex_);
      const auto old_key = trusted_key_id_, old_pem = trusted_public_key_, old_origin = trusted_origin_;
      trusted_key_id_.clear(); trusted_public_key_.clear(); trusted_origin_.clear();
      ok = SaveStateLocked();
      if (!ok) { trusted_key_id_ = old_key; trusted_public_key_ = old_pem; trusted_origin_ = old_origin; }
      publisher_.trusted = false;
    }
    NotifyState(); return ok;
}
bool AppearanceService::SetLocalTheme(const std::string& preset, uint32_t primary) {
    if (!IsAppearancePreset(preset) || primary > 0xffffff) return false;
    bool ok = false;
    { std::lock_guard<std::mutex> lock(mutex_);
      const bool old_local = local_theme_; const auto old_preset = local_preset_; const uint32_t old_primary = local_primary_;
      local_theme_ = true; local_preset_ = preset; local_primary_ = primary;
      ok = SaveStateLocked();
      if (!ok) { local_theme_ = old_local; local_preset_ = old_preset; local_primary_ = old_primary; }
    }
    if (ok) NotifyState();
    return ok;
}
bool AppearanceService::GetLocalTheme(std::string& preset, uint32_t& primary) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!local_theme_ || (boot_candidate_ && pending_.revision != 0 && pending_.revision == selected_revision_)) return false;
    preset = local_preset_; primary = local_primary_; return true;
}
bool AppearanceService::ThemeIsLocal() const { std::lock_guard<std::mutex> lock(mutex_); return local_theme_ || active_.revision == 0; }
bool AppearanceService::ApplyDesiredJson(const std::string& encoded) {
    AppearanceDesired desired;
    if (!DecodeAppearanceDesired(encoded, desired)) return false;
    const auto& key = desired.key_id;
    const uint32_t revision = desired.revision;
    { std::lock_guard<std::mutex> lock(mutex_);
      if (key != trusted_key_id_) {
          cJSON* previous = cJSON_Parse(desired_.c_str()); uint64_t previous_revision = 0;
          const bool stale = Text(previous, "keyId") == key && Integer(previous, "revision", 1, 0xffffffff, previous_revision) && revision <= previous_revision;
          cJSON_Delete(previous);
          if (stale) return true;
      } else {
          const AppearanceRevisionState revisions{active_.revision, pending_.revision, previous_.revision, trial_revision_, rejected_revision_};
          if (!ShouldAcceptAppearanceRevision(revisions, static_cast<uint32_t>(revision), latest_revision_)) return true;
          if (revision == latest_revision_ && desired_.empty() && status_ == "failed") return true;
          if (revision == latest_revision_ && !desired_.empty()) return true;
      }
      latest_revision_ = static_cast<uint32_t>(revision); desired_ = encoded; status_ = "waiting_device"; progress_ = 0; error_.clear();
      download_failures_ = 0; download_retry_at_ms_ = 0;
      if (publisher_.key_id != key) publisher_requested_.store(true);
    }
    NotifyState(); ScheduleWorker(); return true;
}
void AppearanceService::ScheduleWorker() {
    { std::lock_guard<std::mutex> lock(mutex_);
      if ((desired_.empty() && !publisher_requested_.load()) || (download_retry_at_ms_ > NowMs() && !publisher_requested_.load())) return;
    }
    bool expected = false;
    if (!worker_running_.compare_exchange_strong(expected, true)) return;
    if (xTaskCreate(WorkerTask, "appearance_update", 8192, this, 2, nullptr) != pdPASS) {
        worker_running_.store(false); SetStatus("deferred_busy", "worker_unavailable");
    }
}
void AppearanceService::WorkerTask(void* arg) {
    auto* self = static_cast<AppearanceService*>(arg);
    uint32_t handled_revision = 0;
    { std::lock_guard<std::mutex> lock(self->mutex_); handled_revision = self->latest_revision_; }
    self->RunWorker(); self->worker_running_.store(false);
    bool changed = false;
    { std::lock_guard<std::mutex> lock(self->mutex_); changed = !self->desired_.empty() && (self->latest_revision_ != handled_revision || self->status_ == "superseded"); }
    if (changed) self->ScheduleWorker();
    vTaskDelete(nullptr);
}
void AppearanceService::RunWorker() {
    std::function<bool()> gate;
    bool trial = false;
    { std::lock_guard<std::mutex> lock(mutex_); gate = busy_gate_; trial = trial_revision_ != 0; }
    const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const bool busy = gate && gate();
    if (boot_running_.load() || boot_playing_.load() || trial || busy || largest_internal < kAppearanceMinWorkerLargestBlock) {
        ESP_LOGW(TAG, "Appearance update deferred: boot=%d playing=%d trial=%d busy=%d internal_largest=%u",
                 boot_running_.load(), boot_playing_.load(), trial, busy,
                 static_cast<unsigned>(largest_internal));
        SetStatus("deferred_busy", {}, 0); return;
    }
    if (publisher_requested_.exchange(false)) FetchPublisher();
    std::string desired;
    { std::lock_guard<std::mutex> lock(mutex_); desired = desired_; }
    if (desired.empty()) return;
    const bool ok = DownloadDesired(desired);
    { std::lock_guard<std::mutex> lock(mutex_);
      if (desired_ == desired && (ok || status_ == "failed")) desired_.clear();
    }
}
bool AppearanceService::FetchPublisher() {
    DeviceCloudConfig config;
    const auto can_continue = []() { return true; };
    const bool prepared = cloud_.PrepareVoiceConfig(config, can_continue);
    const std::string origin = Origin(config.mqtt_http_base_url);
    { std::lock_guard<std::mutex> lock(mutex_); publisher_.loading = true; publisher_.error.clear(); }
    std::string response, key_id;
    bool ok = prepared && !origin.empty() && !config.aiot_access_token.empty() && FreshJson(cloud_, config, "/api/v1/aiot/appearance/publisher", {}, response, can_continue);
    cJSON* root = ok ? cJSON_Parse(response.c_str()) : nullptr;
    const std::string public_key = Text(root, "publicKey");
    ok = ok && Text(root, "algorithm") == "rsa2048-sha256" && AppearancePublicKeyId(public_key, key_id) && key_id == Text(root, "keyId");
    cJSON_Delete(root);
    { std::lock_guard<std::mutex> lock(mutex_);
      publisher_.loading = false;
      if (ok) {
          publisher_.key_id = key_id; publisher_.public_key = public_key; publisher_.fingerprint = AppearanceKeyFingerprint(key_id); publisher_origin_ = origin;
          publisher_.trusted = key_id == trusted_key_id_ && origin == trusted_origin_;
      } else { publisher_.error = "publisher_unavailable"; publisher_.trusted = false; }
    }
    NotifyState(); return ok;
}
bool AppearanceService::DownloadDesired(const std::string& desired) {
    cJSON* root = cJSON_Parse(desired.c_str()); uint64_t revision = 0;
    const std::string deployment = Text(root, "deploymentId"), release = Text(root, "releaseId"), key_id = Text(root, "keyId"), mode = Text(root, "mode");
    Integer(root, "revision", 1, 0xffffffff, revision); cJSON_Delete(root);
    DeviceCloudConfig config;
    const auto can_continue = [&]() { std::lock_guard<std::mutex> lock(mutex_); return latest_revision_ == revision && desired_ == desired; };
    if (!cloud_.PrepareVoiceConfig(config, can_continue)) {
        if (!can_continue()) SetStatus("superseded", "superseded");
        else if (config.aiot_registered && config.aiot_activated) DeferDownload("credentials_unavailable");
        else SetStatus("failed", "credentials_unavailable");
        return false;
    }
    const std::string origin = Origin(config.mqtt_http_base_url);
    std::string public_key, slot;
    bool trusted = false;
    { std::lock_guard<std::mutex> lock(mutex_);
      trusted = key_id == trusted_key_id_ && origin == trusted_origin_;
      public_key = trusted_public_key_; slot = active_.slot == "a" ? "b" : "a";
    }
    if (!trusted) { SetStatus("awaiting_trust", {}, 0); return false; }
    NotifyState(); SetStatus("downloading", {}, 0);
    cJSON* ticket_body = cJSON_CreateObject();
    const bool body_complete = ticket_body != nullptr && cJSON_AddStringToObject(ticket_body, "deploymentId", deployment.c_str()) != nullptr &&
        cJSON_AddNumberToObject(ticket_body, "revision", revision) != nullptr;
    const std::string body = body_complete ? Encode(ticket_body) : std::string(); cJSON_Delete(ticket_body);
    if (body.empty()) { DeferDownload("metadata_memory_unavailable"); return false; }
    std::string response;
    int request_status = 0;
    if (!FreshJson(cloud_, config, "/api/v1/aiot/appearance/download-ticket", body, response, can_continue, &request_status)) {
        if (!can_continue()) SetStatus("superseded", "superseded");
        else if (TransientHttp(request_status)) DeferDownload("ticket_failed");
        else SetStatus("failed", "ticket_failed");
        return false;
    }
    root = cJSON_Parse(response.c_str()); const std::string ticket = Text(root, "ticketId"); cJSON_Delete(root);
    if (ticket.empty() || ticket.size() > 256) { SetStatus("failed", "ticket_invalid"); return false; }
    const std::string suffix = UrlEncode(deployment) + "?ticketId=" + UrlEncode(ticket);
    std::string manifest, error;
    AppearanceSignedRelease signed_release;
    if (!FreshJson(cloud_, config, "/api/v1/aiot/appearance/manifests/" + suffix, {}, manifest, can_continue, &request_status)) {
        if (!can_continue()) SetStatus("superseded", "superseded");
        else if (TransientHttp(request_status)) DeferDownload("manifest_failed");
        else SetStatus("failed", "manifest_failed");
        return false;
    }
    if (!VerifyAppearanceManifest(manifest, public_key, key_id, config.mqtt_device_key, signed_release, error) ||
        signed_release.deployment_id != deployment || signed_release.release_id != release || signed_release.revision != revision || signed_release.mode != mode) {
        if (files_ != nullptr) files_->WithIoLock([&]() {
            const std::string base = std::string(kDirectory) + "/" + slot;
            files_->DeleteFile(base + ".rap.part"); files_->DeleteFile(base + ".part.meta"); return true;
        });
        SetStatus("failed", error.empty() ? "manifest_failed" : error); return false;
    }
    StoredRelease next;
    next.revision = static_cast<uint32_t>(revision); next.deployment_id = deployment; next.release_id = release; next.key_id = key_id;
    next.mode = mode; next.sha256 = signed_release.package_sha256; next.size = signed_release.package_size; next.slot = mode == "custom" ? slot : "";
    bool ok = true;
    if (mode == "custom") {
        if (files_ == nullptr) { SetStatus("failed", "sd_unavailable"); return false; }
        ok = files_->WithIoLock([&]() {
            if ((!files_->IsMounted() && !files_->Init()) || !files_->CreateDirectory("/rodakos") || !files_->CreateDirectory(kDirectory)) { error = "sd_mount_failed"; return false; }
            const std::string relative = std::string(kDirectory) + "/" + slot;
            const std::string part_path = SdPath(relative + ".rap.part"), meta_path = SdPath(relative + ".part.meta");
            const auto discard = [&]() { std::remove(part_path.c_str()); std::remove(meta_path.c_str()); };
            size_t prefix_size = files_->GetFileSize(relative + ".rap.part");
            std::string saved_meta;
            bool same_partial = prefix_size > 0 && prefix_size <= next.size && ReadSmallFile(meta_path, kJsonLimit, saved_meta);
            cJSON* meta = same_partial ? cJSON_Parse(saved_meta.c_str()) : nullptr;
            AppearanceSignedRelease saved_release;
            std::string saved_error;
            same_partial = same_partial && Text(meta, "origin") == origin &&
                VerifyAppearanceManifest(Text(meta, "manifest"), public_key, key_id, config.mqtt_device_key, saved_release, saved_error) &&
                saved_release.deployment_id == signed_release.deployment_id && saved_release.revision == signed_release.revision &&
                saved_release.release_id == signed_release.release_id && saved_release.package_sha256 == signed_release.package_sha256 &&
                saved_release.package_size == signed_release.package_size;
            cJSON_Delete(meta);
            if (!same_partial) { discard(); prefix_size = 0; }
            FileService::Capacity capacity;
            if (!files_->GetCapacity(capacity) || capacity.free_bytes < next.size - prefix_size + 8192) { error = "sd_full"; return false; }
            meta = cJSON_CreateObject();
            const bool meta_complete = meta != nullptr && cJSON_AddStringToObject(meta, "origin", origin.c_str()) != nullptr &&
                cJSON_AddStringToObject(meta, "manifest", manifest.c_str()) != nullptr;
            const std::string meta_encoded = meta_complete ? Encode(meta) : std::string(); cJSON_Delete(meta);
            if (meta_encoded.empty() || !WriteDurableFile(meta_path + ".new", meta_encoded) ||
                std::rename((meta_path + ".new").c_str(), meta_path.c_str()) != 0) { error = "partial_metadata_failed"; return false; }
            FILE* output = std::fopen(part_path.c_str(), prefix_size == 0 ? "wb" : "ab");
            if (output == nullptr) { error = "sd_write_failed"; return false; }
            Sha256 hash; bool valid = hash.Start(); size_t total = 0;
            std::shared_ptr<uint8_t> buffer(static_cast<uint8_t*>(heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), heap_caps_free);
            valid = valid && buffer != nullptr;
            if (valid && prefix_size > 0) {
                FILE* prefix = std::fopen(part_path.c_str(), "rb"); valid = prefix != nullptr;
                while (valid && total < prefix_size) {
                    const size_t length = std::min<size_t>(4096, prefix_size - total);
                    valid = std::fread(buffer.get(), 1, length, prefix) == length && hash.Update(buffer.get(), length);
                    total += length;
                }
                if (prefix != nullptr) valid = std::fclose(prefix) == 0 && valid;
                if (!valid) error = "prefix_read_failed";
            }
            const std::string url = origin + "/api/v1/aiot/appearance/artifacts/" + suffix;
            esp_http_client_config_t http = {}; http.url = url.c_str(); http.method = HTTP_METHOD_GET; http.timeout_ms = 10000;
            http.buffer_size = 1024; http.disable_auto_redirect = true; http.crt_bundle_attach = esp_crt_bundle_attach; http.user_agent = "RodakOS/appearance-v1";
            RangeHeaders headers; http.event_handler = RangeHeaderEvent; http.user_data = &headers;
            esp_http_client_handle_t client = nullptr;
            for (unsigned attempt = 0; valid && total < next.size && attempt < 2; ++attempt) {
                headers = {};
                client = esp_http_client_init(&http);
                const std::string authorization = "Bearer " + config.aiot_access_token;
                if (client != nullptr) esp_http_client_set_header(client, "Authorization", authorization.c_str());
                const std::string range = "bytes=" + std::to_string(prefix_size) + "-";
                if (client != nullptr && prefix_size > 0) esp_http_client_set_header(client, "Range", range.c_str());
                valid = client != nullptr && esp_http_client_open(client, 0) == ESP_OK;
                const int64_t length = valid ? esp_http_client_fetch_headers(client) : -1;
                const int status = client != nullptr ? esp_http_client_get_status_code(client) : 0;
                if (ShouldRetryAppearanceAuthentication(status, attempt)) {
                    esp_http_client_close(client); esp_http_client_cleanup(client); client = nullptr;
                    cloud_.InvalidateAccessTokenFreshness(config.aiot_access_token);
                    if (!cloud_.PrepareVoiceConfig(config, can_continue) || Origin(config.mqtt_http_base_url) != origin || config.mqtt_device_key != signed_release.device_key) {
                        valid = false; break;
                    }
                    continue;
                }
                const auto action = ValidateAppearanceRangeResponse(status, prefix_size, next.size, length, headers.content_range);
                if (!valid || status == 408 || status == 429 || status >= 500 || status == 0) { valid = false; error = "download_interrupted"; }
                else if (headers.invalid || action == AppearanceRangeAction::kReject) { valid = false; error = "content_range_invalid"; }
                else if (action == AppearanceRangeAction::kRestart && prefix_size > 0) {
                    const bool closed = std::fclose(output) == 0;
                    output = std::fopen(part_path.c_str(), "wb"); total = prefix_size = 0;
                    valid = closed && output != nullptr && hash.Start();
                    if (!valid) error = "sd_write_failed";
                }
                if (valid && prefix_size > 0) ESP_LOGI(TAG, "Resuming appearance revision %u at %u bytes", next.revision, static_cast<unsigned>(prefix_size));
                break;
            }
            int last_report = 0;
            while (valid && total < next.size) {
                const int read = esp_http_client_read(client, reinterpret_cast<char*>(buffer.get()), static_cast<int>(std::min<size_t>(4096, next.size - total)));
                valid = read > 0 && std::fwrite(buffer.get(), 1, static_cast<size_t>(read), output) == static_cast<size_t>(read) && hash.Update(buffer.get(), static_cast<size_t>(read));
                if (!valid) error = read <= 0 ? "download_interrupted" : "sd_write_failed";
                if (read > 0) total += static_cast<size_t>(read);
                { std::lock_guard<std::mutex> lock(mutex_); if (latest_revision_ != next.revision || trusted_key_id_ != next.key_id || trusted_origin_ != origin) { valid = false; error = "superseded"; } progress_ = static_cast<int>(total * 90 / next.size); }
                const int progress = static_cast<int>(total * 90 / next.size);
                if (progress >= last_report + 10) { last_report = progress; NotifyState(); }
            }
            if (valid && client != nullptr && esp_http_client_read(client, reinterpret_cast<char*>(buffer.get()), 1) != 0) { valid = false; error = "download_length_invalid"; }
            std::array<unsigned char, 32> digest = {};
            if (valid && (total != next.size || !hash.Finish(digest) || Sha256ToHex(digest) != next.sha256)) { valid = false; error = "package_hash_invalid"; }
            const bool durable = output != nullptr && std::fflush(output) == 0 && fsync(fileno(output)) == 0;
            const bool closed = output != nullptr && std::fclose(output) == 0; valid = valid && durable && closed;
            if (client != nullptr) { esp_http_client_close(client); esp_http_client_cleanup(client); }
            if (!valid) {
                if (error.empty()) error = "sd_write_failed";
                if (error != "download_interrupted" || !durable || !closed) discard();
                return false;
            }
            FILE* file = std::fopen(SdPath(relative + ".rap.part").c_str(), "rb");
            std::array<uint8_t, 16> header = {}; valid = file != nullptr && std::fread(header.data(), 1, 16, file) == 16;
            const size_t json_size = valid ? Le32(header.data() + 12) : 0;
            valid = valid && json_size != 0 && json_size <= kAppearanceMaxMetadataBytes;
            std::shared_ptr<char> metadata(valid ? static_cast<char*>(heap_caps_malloc(json_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) : nullptr, heap_caps_free);
            AppearancePackageMetadata decoded;
            valid = valid && metadata != nullptr && std::fread(metadata.get(), 1, json_size, file) == json_size && DecodeAppearanceMetadata(header.data(), metadata.get(), json_size, next.size, decoded, error);
            if (file != nullptr) std::fclose(file);
            if (!valid || !WriteDurableFile(SdPath(relative + ".manifest.part"), manifest)) { discard(); if (error.empty()) error = "package_invalid"; return false; }
            const std::string rap_path = SdPath(relative + ".rap");
            const std::string manifest_path = SdPath(relative + ".manifest");
            // The inactive slot may contain an older pending package. FATFS
            // does not replace an existing target during rename, so remove
            // only the inactive destination after the new files are durable.
            std::remove(rap_path.c_str());
            std::remove(manifest_path.c_str());
            if (std::rename(SdPath(relative + ".rap.part").c_str(), rap_path.c_str()) != 0 ||
                std::rename(SdPath(relative + ".manifest.part").c_str(), manifest_path.c_str()) != 0) {
                error = "sd_rename_failed";
                std::remove(SdPath(relative + ".rap.part").c_str());
                std::remove(SdPath(relative + ".manifest.part").c_str());
                return false;
            }
            std::remove(meta_path.c_str());
            return true;
        });
    }
    if (!ok) {
        if (error == "download_interrupted") DeferDownload(error);
        else SetStatus(error == "superseded" ? "superseded" : "failed", error);
        return false;
    }
    { std::lock_guard<std::mutex> lock(mutex_);
      if (latest_revision_ != next.revision || trusted_key_id_ != next.key_id || trusted_origin_ != origin) { status_ = "superseded"; return false; }
      const StoredRelease old = pending_; pending_ = next;
      if (!SaveStateLocked()) { pending_ = old; status_ = "failed"; error_ = "state_commit_failed"; ok = false; }
      else { status_ = "pending_reboot"; error_.clear(); progress_ = 100; }
    }
    NotifyState(); return ok;
}
std::string AppearanceService::ReportedJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON* root = cJSON_CreateObject(); cJSON_AddNumberToObject(root, "schemaVersion", 1);
    cJSON* capabilities = cJSON_CreateObject(); cJSON_AddNumberToObject(capabilities, "schemaVersion", 1);
    cJSON_AddNumberToObject(capabilities, "width", 320); cJSON_AddNumberToObject(capabilities, "height", 240);
    cJSON_AddNumberToObject(capabilities, "maxPackageBytes", kAppearanceMaxPackageBytes); cJSON_AddNumberToObject(capabilities, "maxUnits", 32); cJSON_AddNumberToObject(capabilities, "maxDurationMs", kAppearanceMaxDurationMs);
    cJSON_AddItemToObject(root, "capabilities", capabilities); cJSON_AddNumberToObject(root, "activeRevision", active_.revision);
    const uint32_t attempted = status_ == "failed" || status_ == "fallback" || status_ == "awaiting_trust" || status_ == "deferred_busy" || status_ == "downloading" || status_ == "waiting_device" ? latest_revision_ : pending_.revision;
    cJSON_AddNumberToObject(root, "pendingRevision", attempted); cJSON_AddStringToObject(root, "status", status_.c_str()); cJSON_AddNumberToObject(root, "progress", progress_);
    if (!error_.empty()) cJSON_AddStringToObject(root, "error", error_.c_str());
    if (!fallback_reason_.empty()) cJSON_AddStringToObject(root, "fallbackReason", fallback_reason_.c_str());
    if (!trusted_key_id_.empty()) cJSON_AddStringToObject(root, "publisherKeyId", trusted_key_id_.c_str());
    cJSON_AddStringToObject(root, "themeSource", local_theme_ || active_.revision == 0 ? "local" : "remote");
    cJSON_AddNumberToObject(root, "loadMs", load_ms_); cJSON_AddNumberToObject(root, "animationMs", animation_ms_);
    const std::string encoded = Encode(root); cJSON_Delete(root); return encoded;
}
}  // namespace rodakos
