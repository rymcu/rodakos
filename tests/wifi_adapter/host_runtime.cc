#include "host_runtime.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct HostTimer { void* id; TimerCallbackFunction_t callback; bool active = false; bool deleting = false; };
esp_event_base_t WIFI_EVENT = "wifi";
esp_event_base_t IP_EVENT = "ip";
namespace {
struct Registration { esp_event_base_t base; esp_event_handler_t callback; void* arg; };
struct Event { esp_event_base_t base; int32_t id; std::vector<uint8_t> payload; };
std::mutex mutex, event_mutex;
std::condition_variable timer_idle;
std::vector<std::unique_ptr<Registration>> handlers;
std::vector<std::unique_ptr<HostTimer>> timers;
std::deque<Event> events;
esp_netif_t netif;
std::atomic<int64_t> now{0};
std::atomic<int> connect_calls{0}, disconnect_calls{0}, driver_calls{0}, overlaps{0};
std::atomic<int> ip_info_calls{0};
int timers_in_flight = 0;
bool timer_deletion = false, associated = false, event_post_failure = false;
std::string configured_ssid, associated_ssid;
esp_err_t connect_result = ESP_OK, disconnect_result = ESP_ERR_WIFI_NOT_CONNECT;
esp_err_t ap_info_result = ESP_OK;
esp_err_t ip_info_result = ESP_OK;
uint32_t live_ip = 0;
std::function<void()> before_connect, before_timer;
thread_local bool event_context = false;
struct DriverCall {
    DriverCall() { if (driver_calls.fetch_add(1) != 0) ++overlaps; }
    ~DriverCall() { --driver_calls; }
};
void Enqueue(esp_event_base_t base, int id, const void* payload, size_t size) {
    std::lock_guard<std::mutex> lock(mutex);
    Event event{base, id, {}};
    if (size) event.payload.assign(static_cast<const uint8_t*>(payload), static_cast<const uint8_t*>(payload) + size);
    events.push_back(std::move(event));
}
}
namespace wifi_host {
void Reset() {
    std::lock_guard<std::mutex> lock(mutex);
    handlers.clear(); timers.clear(); events.clear();
    now = 0; connect_calls = 0; disconnect_calls = 0; overlaps = 0; driver_calls = 0;
    timer_deletion = associated = event_post_failure = false;
    configured_ssid.clear(); associated_ssid.clear();
    connect_result = ESP_OK; disconnect_result = ESP_ERR_WIFI_NOT_CONNECT;
    ap_info_result = ESP_OK; ip_info_result = ESP_OK; live_ip = 0; ip_info_calls = 0;
    before_connect = {}; before_timer = {}; timers_in_flight = 0;
}
void Drain() {
    std::lock_guard<std::mutex> dispatch(event_mutex);
    for (;;) {
        Event event;
        std::vector<Registration> callbacks;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (events.empty()) break;
            event = std::move(events.front()); events.pop_front();
            for (auto& handler : handlers) if (handler->base == event.base) callbacks.push_back(*handler);
        }
        event_context = true;
        for (const auto& handler : callbacks)
            handler.callback(handler.arg, event.base, event.id, event.payload.empty() ? nullptr : event.payload.data());
        event_context = false;
    }
}
void Advance(uint32_t milliseconds, bool drain) {
    now += static_cast<int64_t>(milliseconds) * 1000;
    std::vector<HostTimer*> selected;
    std::function<void()> hook;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& timer : timers) if (timer->active && !timer->deleting) selected.push_back(timer.get());
        timers_in_flight += static_cast<int>(selected.size());
        hook = before_timer;
    }
    for (auto* timer : selected) {
        if (hook) hook();
        timer->callback(timer);
        std::lock_guard<std::mutex> lock(mutex);
        --timers_in_flight;
        timer_idle.notify_all();
    }
    if (drain) Drain();
}
void SetConnectedDriver(const std::string& ssid) {
    std::lock_guard<std::mutex> lock(mutex); associated = true; associated_ssid = ssid;
}
void Connected(const std::string& ssid) {
    SetConnectedDriver(ssid);
    StaleConnected(ssid);
}
void StaleConnected(const std::string& ssid) {
    wifi_event_sta_connected_t event{};
    event.ssid_len = static_cast<uint8_t>(ssid.size());
    std::memcpy(event.ssid, ssid.data(), std::min(ssid.size(), sizeof(event.ssid)));
    Enqueue(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &event, sizeof(event)); Drain();
}
void GotIP(uint32_t ip) {
    { std::lock_guard<std::mutex> lock(mutex); live_ip = ip; }
    StaleGotIP(ip);
}
void StaleGotIP(uint32_t ip) {
    ip_event_got_ip_t event{}; event.esp_netif = &netif; event.ip_info.ip.addr = ip;
    Enqueue(IP_EVENT, IP_EVENT_STA_GOT_IP, &event, sizeof(event)); Drain();
}
void LostIP() {
    { std::lock_guard<std::mutex> lock(mutex); live_ip = 0; }
    StaleLostIP();
}
void StaleLostIP() {
    ip_event_got_ip_t event{}; event.esp_netif = &netif;
    Enqueue(IP_EVENT, IP_EVENT_STA_LOST_IP, &event, sizeof(event)); Drain();
}
void ForeignLostIP() {
    static esp_netif_t foreign_netif;
    ip_event_got_ip_t event{}; event.esp_netif = &foreign_netif;
    Enqueue(IP_EVENT, IP_EVENT_STA_LOST_IP, &event, sizeof(event)); Drain();
}
void MissingLostIP() {
    Enqueue(IP_EVENT, IP_EVENT_STA_LOST_IP, nullptr, 0);
    ip_event_got_ip_t event{};
    Enqueue(IP_EVENT, IP_EVENT_STA_LOST_IP, &event, sizeof(event)); Drain();
}
void Disconnected(const std::string& ssid, bool still_associated) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!still_associated) associated = false;
    }
    wifi_event_sta_disconnected_t event{};
    event.ssid_len = static_cast<uint8_t>(ssid.size());
    std::memcpy(event.ssid, ssid.data(), std::min(ssid.size(), sizeof(event.ssid)));
    Enqueue(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event, sizeof(event)); Drain();
}
void SetConnectResult(esp_err_t error) { std::lock_guard<std::mutex> lock(mutex); connect_result = error; }
void SetDisconnectResult(esp_err_t error) { std::lock_guard<std::mutex> lock(mutex); disconnect_result = error; }
void SetAPInfoResult(esp_err_t error) { std::lock_guard<std::mutex> lock(mutex); ap_info_result = error; }
void SetIPInfoResult(esp_err_t error) { std::lock_guard<std::mutex> lock(mutex); ip_info_result = error; }
void SetEventPostFailure(bool value) { std::lock_guard<std::mutex> lock(mutex); event_post_failure = value; }
void BeforeConnect(std::function<void()> hook) { std::lock_guard<std::mutex> lock(mutex); before_connect = std::move(hook); }
void BeforeTimer(std::function<void()> hook) { std::lock_guard<std::mutex> lock(mutex); before_timer = std::move(hook); }
int ConnectCalls() { return connect_calls; }
int DisconnectCalls() { return disconnect_calls; }
int DriverOverlaps() { return overlaps; }
int IPInfoCalls() { return ip_info_calls; }
int LiveTimers() { std::lock_guard<std::mutex> lock(mutex); return static_cast<int>(timers.size()); }
bool TimerDeletionRequested() { std::lock_guard<std::mutex> lock(mutex); return timer_deletion; }
std::string ConfiguredSSID() { std::lock_guard<std::mutex> lock(mutex); return configured_ssid; }
}
const char* esp_err_to_name(esp_err_t) { return "fake-error"; }
esp_err_t nvs_flash_init() { return ESP_OK; }
esp_err_t nvs_flash_erase() { return ESP_OK; }
esp_err_t esp_netif_init() { return ESP_OK; }
esp_err_t esp_event_loop_create_default() { return ESP_OK; }
esp_netif_t* esp_netif_create_default_wifi_sta() { DriverCall call; return &netif; }
void esp_netif_destroy_default_wifi(void*) { DriverCall call; }
esp_err_t esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t* result) {
    DriverCall call; ++ip_info_calls; std::lock_guard<std::mutex> lock(mutex);
    if (ip_info_result != ESP_OK) return ip_info_result;
    result->ip.addr = live_ip; return ESP_OK;
}
esp_err_t esp_wifi_init(const wifi_init_config_t*) { DriverCall call; return ESP_OK; }
esp_err_t esp_wifi_deinit() { DriverCall call; return ESP_OK; }
esp_err_t esp_wifi_start() { DriverCall call; return ESP_OK; }
esp_err_t esp_wifi_stop() { DriverCall call; return ESP_OK; }
esp_err_t esp_wifi_set_mode(int) { DriverCall call; return ESP_OK; }
esp_err_t esp_wifi_set_config(int, const wifi_config_t* config) {
    DriverCall call; std::lock_guard<std::mutex> lock(mutex);
    configured_ssid.assign(reinterpret_cast<const char*>(config->sta.ssid), strnlen(reinterpret_cast<const char*>(config->sta.ssid), 32));
    return ESP_OK;
}
esp_err_t esp_wifi_connect() {
    DriverCall call; ++connect_calls;
    std::function<void()> hook; esp_err_t result;
    { std::lock_guard<std::mutex> lock(mutex); hook = before_connect; result = connect_result; }
    if (hook) hook();
    return result;
}
esp_err_t esp_wifi_disconnect() {
    DriverCall call; ++disconnect_calls;
    std::lock_guard<std::mutex> lock(mutex);
    if (disconnect_result == ESP_ERR_WIFI_NOT_CONNECT) associated = false;
    return disconnect_result;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t* ap) {
    DriverCall call; std::lock_guard<std::mutex> lock(mutex);
    if (ap_info_result != ESP_OK) return ap_info_result;
    if (!associated) return ESP_ERR_WIFI_NOT_CONNECT;
    std::memcpy(ap->ssid, associated_ssid.data(), std::min(associated_ssid.size(), sizeof(ap->ssid)));
    return ESP_OK;
}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t*, bool) { DriverCall call; return ESP_OK; }
esp_err_t esp_wifi_scan_stop() { DriverCall call; return ESP_OK; }
esp_err_t esp_wifi_clear_ap_list() { DriverCall call; return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_num(uint16_t* count) { DriverCall call; *count = 0; return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t*, wifi_ap_record_t*) { DriverCall call; return ESP_OK; }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t, esp_event_handler_t callback,
                                              void* arg, esp_event_handler_instance_t* instance) {
    std::lock_guard<std::mutex> lock(mutex);
    auto item = std::make_unique<Registration>(Registration{base, callback, arg});
    *instance = item.get(); handlers.push_back(std::move(item)); return ESP_OK;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t, esp_event_handler_instance_t instance) {
    std::lock_guard<std::mutex> lock(mutex);
    handlers.erase(std::remove_if(handlers.begin(), handlers.end(), [instance](const auto& item) {
        return item.get() == instance;
    }), handlers.end()); return ESP_OK;
}
esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void* data, size_t size, TickType_t) {
    { std::lock_guard<std::mutex> lock(mutex); if (event_post_failure) return ESP_FAIL; }
    Enqueue(base, id, data, size); return ESP_OK;
}
int64_t esp_timer_get_time() { return now; }
TaskHandle_t xTaskGetCurrentTaskHandle() {
    static int event_id; static thread_local int task_id;
    return event_context ? &event_id : &task_id;
}
void vTaskDelay(TickType_t) { std::this_thread::yield(); }
TimerHandle_t xTimerCreate(const char*, TickType_t, BaseType_t, void* id, TimerCallbackFunction_t callback) {
    std::lock_guard<std::mutex> lock(mutex);
    auto timer = std::make_unique<HostTimer>(HostTimer{id, callback});
    auto* pointer = timer.get(); timers.push_back(std::move(timer)); return pointer;
}
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t) {
    std::lock_guard<std::mutex> lock(mutex); timer->active = true; return pdPASS;
}
BaseType_t xTimerDelete(TimerHandle_t timer, TickType_t) {
    std::lock_guard<std::mutex> lock(mutex); timer->deleting = true; timer->active = false;
    timer_deletion = true; return pdPASS;
}
BaseType_t xTimerPendFunctionCall(PendedFunction_t callback, void* arg, uint32_t value, TickType_t) {
    {
        std::unique_lock<std::mutex> lock(mutex);
        timer_idle.wait(lock, []() { return timers_in_flight == 0; });
        timers.erase(std::remove_if(timers.begin(), timers.end(), [](const auto& item) {
            return item->deleting;
        }), timers.end());
    }
    callback(arg, value); return pdPASS;
}
void* pvTimerGetTimerID(TimerHandle_t timer) { return timer->id; }
