#include "host_runtime.h"
#include "phone_os/battery_monitor.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

struct HostMqttClient {
    std::recursive_mutex api_mutex;
    std::mutex events_mutex;
    std::condition_variable changed;
    std::deque<esp_mqtt_event_t> events;
    esp_event_handler_t callback = nullptr;
    void* context = nullptr;
    bool running = false;
    bool destroyed = false;
    unsigned id = 0;
    std::atomic<unsigned> credential_revision{0};
    std::thread thread;
};

namespace {
std::mutex state_mutex;
std::vector<std::unique_ptr<HostMqttClient>> clients;
std::vector<std::thread> workers;
std::vector<mqtt_host::Publication> publications;
rodakos::DeviceCloudConfig stored_config;
std::atomic<HostMqttClient*> current_client{nullptr};
std::atomic<bool> hold_user_events{false};
std::atomic<unsigned> restart_count{0};
thread_local bool in_sdk_callback = false;
std::mutex dequeue_mutex;
std::condition_variable dequeue_changed;
bool pause_dequeue = false;
bool dequeue_parked = false;

void QueueEvent(HostMqttClient* client, esp_mqtt_event_id_t id) {
    std::lock_guard<std::mutex> lock(client->events_mutex);
    esp_mqtt_event_t event;
    event.client = client;
    event.event_id = id;
    client->events.push_back(event);
    client->changed.notify_all();
}

void Dispatch(esp_mqtt_event_t event) {
    HostMqttClient* client = event.client;
    // 与真实 ESP-MQTT 一致：持 SDK 递归 API 锁同步运行已注册回调。
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    if (client->destroyed) throw std::runtime_error("callback used destroyed MQTT client");
    in_sdk_callback = true;
    client->callback(client->context, "mqtt", event.event_id, &event);
    in_sdk_callback = false;
}
}

int xTaskCreate(TaskFunction_t entry, const char*, uint32_t, void* argument,
                 uint32_t, TaskHandle_t* task) {
    *task = argument;
    workers.emplace_back([=]() { entry(argument); });
    return pdPASS;
}
void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min<TickType_t>(ticks, 2)));
}
QueueHandle_t xQueueCreate(unsigned capacity, unsigned) {
    auto* queue = new HostQueue;
    queue->capacity = capacity;
    return queue;
}
int xQueueSend(QueueHandle_t queue, const void* item, TickType_t) {
    std::lock_guard<std::mutex> lock(queue->mutex);
    if (queue->items.size() >= queue->capacity) return pdFALSE;
    queue->items.push_back(*static_cast<void* const*>(item));
    queue->changed.notify_all();
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void* output, TickType_t wait) {
    {
        std::unique_lock<std::mutex> lock(queue->mutex);
        if (!queue->changed.wait_for(lock, std::chrono::milliseconds(wait),
                                    [&]() { return !queue->items.empty(); })) return pdFALSE;
        *static_cast<void**>(output) = queue->items.front();
        queue->items.pop_front();
    }
    if (wait != 0) {
        std::unique_lock<std::mutex> lock(dequeue_mutex);
        if (pause_dequeue) {
            dequeue_parked = true;
            dequeue_changed.notify_all();
            dequeue_changed.wait(lock, []() { return !pause_dequeue; });
            dequeue_parked = false;
        }
    }
    return pdTRUE;
}
void esp_restart() { ++restart_count; }

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t*) {
    std::lock_guard<std::mutex> lock(state_mutex);
    auto client = std::make_unique<HostMqttClient>();
    client->id = static_cast<unsigned>(clients.size() + 1);
    HostMqttClient* pointer = client.get();
    clients.push_back(std::move(client));
    current_client = pointer;
    return pointer;
}
int esp_mqtt_client_register_event(HostMqttClient* client, esp_mqtt_event_id_t,
                                   esp_event_handler_t callback, void* context) {
    client->callback = callback;
    client->context = context;
    return ESP_OK;
}
int esp_mqtt_client_start(HostMqttClient* client) {
    client->running = true;
    client->thread = std::thread([client]() {
        while (true) {
            esp_mqtt_event_t event;
            {
                std::unique_lock<std::mutex> lock(client->events_mutex);
                client->changed.wait_for(lock, std::chrono::milliseconds(2), [&]() {
                    return !client->running || (!client->events.empty() && !hold_user_events);
                });
                if (!client->running) return;
                auto next = std::find_if(client->events.begin(), client->events.end(), [](const auto& item) {
                    return !hold_user_events || item.event_id != MQTT_USER_EVENT;
                });
                if (next == client->events.end()) continue;
                event = *next;
                client->events.erase(next);
            }
            Dispatch(event);
        }
    });
    QueueEvent(client, MQTT_EVENT_CONNECTED);
    return ESP_OK;
}
int esp_mqtt_client_stop(HostMqttClient* client) {
    {
        std::lock_guard<std::mutex> lock(client->events_mutex);
        client->running = false;
        client->changed.notify_all();
    }
    if (client->thread.joinable()) client->thread.join();
    return ESP_OK;
}
int esp_mqtt_client_destroy(HostMqttClient* client) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    client->destroyed = true;
    return ESP_OK;
}
int esp_mqtt_client_reconnect(HostMqttClient* client) { QueueEvent(client, MQTT_EVENT_CONNECTED); return ESP_OK; }
int esp_mqtt_set_config(HostMqttClient* client, const esp_mqtt_client_config_t*) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    ++client->credential_revision;
    return ESP_OK;
}
int esp_mqtt_client_get_outbox_size(HostMqttClient*) { return 0; }
int esp_mqtt_client_subscribe(HostMqttClient* client, const char*, int) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    return 1;
}
int esp_mqtt_client_enqueue(HostMqttClient* client, const char* topic, const char* payload,
                            int length, int, int, bool) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    if (client->destroyed) throw std::runtime_error("enqueue used destroyed MQTT client");
    std::lock_guard<std::mutex> state_lock(state_mutex);
    publications.push_back({topic, std::string(payload, length), client->id,
                            client->credential_revision, in_sdk_callback});
    return static_cast<int>(publications.size());
}
int esp_mqtt_dispatch_custom_event(HostMqttClient* client, esp_mqtt_event_t* event) {
    // 故意不取 api_mutex；真实 SDK 这里只投递事件，不能同步调用回调。
    std::lock_guard<std::mutex> lock(client->events_mutex);
    if (!client->running || client->destroyed) return ESP_FAIL;
    client->events.push_back(*event);
    client->changed.notify_all();
    return ESP_OK;
}

namespace rodakos {
bool DeviceCloudConfigService::Load(DeviceCloudConfig& config) { config = mqtt_host::Config(); return true; }
bool DeviceCloudConfigService::Refresh(DeviceCloudConfig& config) { config = mqtt_host::Config(); return true; }
std::string DeviceCloudConfigService::last_error() const { return "host config unavailable"; }
BatteryMonitor::~BatteryMonitor() = default;
BatterySnapshot BatteryMonitor::Read() { return {}; }
}

namespace mqtt_host {
void Reset() {
    JoinWorkers();
    std::lock_guard<std::mutex> lock(state_mutex);
    clients.clear();
    publications.clear();
    stored_config = {};
    stored_config.mqtt_protocol_version = 2;
    stored_config.mqtt_broker_address = "host-broker";
    stored_config.mqtt_broker_port = 1883;
    stored_config.mqtt_username = "test-device";
    stored_config.mqtt_device_key = "test-device";
    stored_config.mqtt_password = "test-token-1";
    stored_config.aiot_device_secret = "test-binding-1";
    stored_config.aiot_registered = true;
    stored_config.aiot_activated = true;
    stored_config.mqtt_http_base_url = "http://host-broker";
    stored_config.provisioning_url = "http://host-broker/bootstrap";
    stored_config.mqtt_topic_shadow_desired = "devices/test-device/shadow/desired";
    stored_config.mqtt_topic_shadow_report = "devices/test-device/shadow/report";
    stored_config.mqtt_topic_telemetry = "devices/test-device/telemetry";
    stored_config.mqtt_topic_ota_notify = "devices/test-device/ota/notify";
    stored_config.mqtt_topic_ota_progress = "devices/test-device/ota/progress";
    stored_config.mqtt_topic_commands = "devices/test-device/commands/+";
    stored_config.mqtt_topic_pc_status = "devices/test-device/pc_status";
    stored_config.has_mqtt_config = true;
    current_client = nullptr;
    hold_user_events = false;
    restart_count = 0;
}
void JoinWorkers() {
    PauseDequeue(false);
    for (auto& worker : workers) if (worker.joinable()) worker.join();
    workers.clear();
}
void SetConfig(const rodakos::DeviceCloudConfig& config) {
    std::lock_guard<std::mutex> lock(state_mutex);
    stored_config = config;
}
rodakos::DeviceCloudConfig Config() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return stored_config;
}
HostMqttClient* CurrentClient() { return current_client; }
void Deliver(esp_mqtt_event_t event) {
    if (event.client == nullptr) event.client = CurrentClient();
    Dispatch(event);
}
void Fragment(const std::string& topic, const std::string& bytes, int offset, int total) {
    esp_mqtt_event_t event;
    event.event_id = MQTT_EVENT_DATA;
    event.topic = topic.c_str();
    event.topic_len = static_cast<int>(topic.size());
    event.data = bytes.data();
    event.data_len = static_cast<int>(bytes.size());
    event.current_data_offset = offset;
    event.total_data_len = total;
    Deliver(event);
}
void Message(const std::string& topic, const std::string& payload, bool fragmented) {
    const int length = static_cast<int>(payload.size());
    if (!fragmented) { Fragment(topic, payload, 0, length); return; }
    const int middle = length / 2;
    Fragment(topic, payload.substr(0, middle), 0, length);
    Fragment("", payload.substr(middle), middle, length);
}
void Disconnect() { esp_mqtt_event_t event; event.event_id = MQTT_EVENT_DISCONNECTED; Deliver(event); }
void Connect() { esp_mqtt_event_t event; event.event_id = MQTT_EVENT_CONNECTED; Deliver(event); }
void RejectCredentials() {
    esp_mqtt_error_codes_t error;
    error.error_type = MQTT_ERROR_TYPE_CONNECTION_REFUSED;
    error.connect_return_code = MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED;
    esp_mqtt_event_t event;
    event.event_id = MQTT_EVENT_ERROR;
    event.error_handle = &error;
    Deliver(event);
}
void HoldUserEvents(bool hold) { hold_user_events = hold; }
size_t PendingUserEvents() {
    auto* client = CurrentClient();
    std::lock_guard<std::mutex> lock(client->events_mutex);
    return std::count_if(client->events.begin(), client->events.end(),
        [](const auto& event) { return event.event_id == MQTT_USER_EVENT; });
}
std::vector<Publication> Publications() { std::lock_guard<std::mutex> lock(state_mutex); return publications; }
size_t ReceiptCount() {
    const auto all = Publications();
    return std::count_if(all.begin(), all.end(), [](const auto& item) {
        return item.topic.find("/effects/receipt") != std::string::npos;
    });
}
unsigned CredentialRevision() { return CurrentClient()->credential_revision; }
unsigned Restarts() { return restart_count; }
void PauseDequeue(bool pause) {
    std::lock_guard<std::mutex> lock(dequeue_mutex);
    pause_dequeue = pause;
    dequeue_changed.notify_all();
}
bool WaitDequeued() {
    std::unique_lock<std::mutex> lock(dequeue_mutex);
    return dequeue_changed.wait_for(lock, std::chrono::seconds(3), []() { return dequeue_parked; });
}
bool WaitUntil(const std::function<bool()>& predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < end) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}
}
