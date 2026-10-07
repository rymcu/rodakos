#include "host_runtime.h"
#include "diagnostics_support.h"
#include "phone_os/battery_monitor.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

struct HostMqttClient {
    std::recursive_mutex api_mutex;
    std::mutex events_mutex;
    std::condition_variable changed;
    std::deque<esp_mqtt_event_t> events;
    std::deque<esp_mqtt_event_t> custom_events;
    bool legacy_shared_events = false;
    unsigned dropped_native_events = 0;
    esp_event_handler_t callback = nullptr;
    void* context = nullptr;
    bool running = false;
    bool destroyed = false;
    bool connected = false;
    unsigned transport_epoch = 0;
    std::deque<mqtt_host::Publication> outbox;
    unsigned id = 0;
    std::atomic<unsigned> credential_revision{0};
    std::thread thread;
};

namespace {
std::mutex state_mutex;
std::vector<std::unique_ptr<HostMqttClient>> clients;
std::vector<std::thread> workers;
std::vector<mqtt_host::Publication> publications;
std::vector<mqtt_host::Publication> wire_publications;
std::vector<mqtt_host::Publication> direct_publish_attempts;
rodakos::DeviceCloudConfig stored_config;
std::string tls_uri;
const char* tls_certificate = nullptr;
const char* tls_common_name = nullptr;
std::atomic<HostMqttClient*> current_client{nullptr};
std::atomic<bool> hold_user_events{false};
std::atomic<unsigned> restart_count{0};
std::atomic<bool> hold_wire{false};
std::atomic<bool> fail_next_direct_publish{false};
std::atomic<bool> fail_next_custom_event{false};
std::atomic<bool> fail_next_custom_transfer{false};
std::atomic<uint64_t> queued_sequence{0};
std::atomic<uint64_t> completed_sequence{0};
thread_local bool in_sdk_callback = false;
std::mutex dequeue_mutex;
std::condition_variable dequeue_changed;
bool pause_dequeue = false;
bool dequeue_parked = false;
std::mutex direct_mutex;
std::condition_variable direct_changed;
bool direct_paused = false;
bool direct_entered = false;
thread_local void (*before_depth_sample)(QueueHandle_t, void*) = nullptr;
thread_local void* before_depth_context = nullptr;

void FlushOutbox(HostMqttClient* client) {
    // 调用者持 SDK API 锁；断线期间保留 outbox，重连才再次出线。
    if (!client->connected || hold_wire) return;
    std::lock_guard<std::mutex> lock(state_mutex);
    while (!client->outbox.empty()) {
        auto sent = client->outbox.front();
        sent.transport_epoch = client->transport_epoch;
        sent.credential_revision = client->credential_revision;
        wire_publications.push_back(std::move(sent));
        client->outbox.pop_front();
    }
}

void QueueEvent(HostMqttClient* client, esp_mqtt_event_id_t id) {
    std::lock_guard<std::recursive_mutex> api_lock(client->api_mutex);
    if (id == MQTT_EVENT_CONNECTED) {
        client->connected = true;
        ++client->transport_epoch;
    }
    if (id == MQTT_EVENT_DISCONNECTED) client->connected = false;
    std::lock_guard<std::mutex> lock(client->events_mutex);
    esp_mqtt_event_t event;
    event.client = client;
    event.event_id = id;
    if (client->events.size() == 1) ++client->dropped_native_events;
    else client->events.push_back(event);
    client->changed.notify_all();
}

void Dispatch(esp_mqtt_event_t event) {
    HostMqttClient* client = event.client;
    // 与真实 ESP-MQTT 一致：持 SDK 递归 API 锁同步运行已注册回调。
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    if (client->destroyed) throw std::runtime_error("callback used destroyed MQTT client");
    const bool previous_callback = in_sdk_callback;
    in_sdk_callback = true;
    client->callback(client->context, "mqtt", event.event_id, &event);
    in_sdk_callback = previous_callback;
    FlushOutbox(client);
}

void RunNativeEvents(HostMqttClient* client) {
    // esp_event_loop_run(..., 0) 只消费一条，callback 仍可同步嵌套运行自己的 native event。
    esp_mqtt_event_t event;
    {
        std::lock_guard<std::mutex> lock(client->events_mutex);
        if (client->events.empty()) return;
        event = client->events.front();
        client->events.pop_front();
    }
    Dispatch(event);
}

void PostNativeAndRun(esp_mqtt_event_t event) {
    auto* client = event.client;
    std::lock_guard<std::recursive_mutex> api_lock(client->api_mutex);
    if (event.event_id == MQTT_EVENT_CONNECTED) {
        client->connected = true;
        ++client->transport_epoch;
    }
    if (event.event_id == MQTT_EVENT_DISCONNECTED) client->connected = false;
    {
        std::lock_guard<std::mutex> lock(client->events_mutex);
        // 原 SDK 会忽略满队列时 lifecycle post 的错误，再运行已有队列。
        if (client->events.size() == 1) ++client->dropped_native_events;
        else client->events.push_back(event);
    }
    RunNativeEvents(client);
}

bool RunOneCustomEvent(HostMqttClient* client) {
    {
        std::lock_guard<std::mutex> lock(client->events_mutex);
        if (client->legacy_shared_events) {
            if (client->events.empty()) return false;
        } else {
            if (client->custom_events.empty()) return false;
            const auto event = client->custom_events.front();
            client->custom_events.pop_front();
            if (!client->events.empty() || fail_next_custom_transfer.exchange(false)) {
                // overlay 单轮只尝试一次；转投失败保留唤醒，下一轮再试。
                if (client->custom_events.empty()) client->custom_events.push_front(event);
                return true;
            }
            client->events.push_back(event);
        }
    }
    RunNativeEvents(client);
    return true;
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
int xQueueSend(QueueHandle_t queue, const void* item, TickType_t wait) {
    std::lock_guard<std::mutex> lock(queue->mutex);
    ++queue->send_attempts;
    queue->last_send_wait = wait;
    if (queue->items.size() >= queue->capacity) return pdFALSE;
    ++queue->send_accepted;
    queue->items.push_back({*static_cast<void* const*>(item), ++queued_sequence});
    queue->changed.notify_all();
    return pdTRUE;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
    if (before_depth_sample != nullptr) {
        const auto callback = before_depth_sample;
        before_depth_sample = nullptr;
        callback(queue, before_depth_context);
    }
    std::lock_guard<std::mutex> lock(queue->mutex);
    return static_cast<UBaseType_t>(queue->items.size());
}
int xQueueReceive(QueueHandle_t queue, void* output, TickType_t wait) {
    thread_local uint64_t previous_sequence = 0;
    if (wait != 0 && previous_sequence != 0) {
        completed_sequence.store(previous_sequence);
        previous_sequence = 0;
    }
    {
        std::unique_lock<std::mutex> lock(queue->mutex);
        if (!queue->changed.wait_for(lock, std::chrono::milliseconds(wait),
                                    [&]() { return !queue->items.empty(); })) return pdFALSE;
        *static_cast<void**>(output) = queue->items.front().value;
        if (wait != 0) previous_sequence = queue->items.front().sequence;
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

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config) {
    std::lock_guard<std::mutex> lock(state_mutex);
    tls_uri = config->broker.address.uri;
    tls_certificate = config->broker.verification.certificate;
    tls_common_name = config->broker.verification.common_name;
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
            {
                std::unique_lock<std::mutex> lock(client->events_mutex);
                client->changed.wait_for(lock, std::chrono::milliseconds(2), [&]() {
                    return !client->running ||
                        (!client->events.empty() && (!hold_user_events || client->events.front().event_id != MQTT_USER_EVENT)) ||
                        (!client->custom_events.empty() && !hold_user_events);
                });
                if (!client->running) return;
            }
            std::lock_guard<std::recursive_mutex> api_lock(client->api_mutex);
            bool native_ready = false;
            {
                std::lock_guard<std::mutex> lock(client->events_mutex);
                native_ready = !client->events.empty() &&
                    (!hold_user_events || client->events.front().event_id != MQTT_USER_EVENT);
            }
            if (native_ready) RunNativeEvents(client);
            else if (!hold_user_events) RunOneCustomEvent(client);
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
    {
        std::lock_guard<std::mutex> lock(client->events_mutex);
        client->custom_events.clear();
        client->events.clear();
    }
    return ESP_OK;
}
int esp_mqtt_client_destroy(HostMqttClient* client) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    client->destroyed = true;
    client->connected = false;
    client->outbox.clear();
    return ESP_OK;
}
int esp_mqtt_client_reconnect(HostMqttClient* client) { QueueEvent(client, MQTT_EVENT_CONNECTED); return ESP_OK; }
int esp_mqtt_set_config(HostMqttClient* client, const esp_mqtt_client_config_t*) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    ++client->credential_revision;
    return ESP_OK;
}
int esp_mqtt_client_get_outbox_size(HostMqttClient* client) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    size_t size = 0;
    for (const auto& item : client->outbox) size += item.payload.size();
    return static_cast<int>(size);
}
int esp_mqtt_client_subscribe(HostMqttClient* client, const char*, int) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    return 1;
}
int esp_mqtt_client_enqueue(HostMqttClient* client, const char* topic, const char* payload,
                            int length, int, int, bool) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    if (client->destroyed) throw std::runtime_error("enqueue used destroyed MQTT client");
    const mqtt_host::Publication item{topic, std::string(payload, length), client->id,
                                      client->credential_revision, in_sdk_callback, true, client->transport_epoch};
    int message_id = 0;
    {
        std::lock_guard<std::mutex> state_lock(state_mutex);
        publications.push_back(item);
        message_id = static_cast<int>(publications.size());
    }
    client->outbox.push_back(item);
    FlushOutbox(client);
    return message_id;
}
int esp_mqtt_client_publish(HostMqttClient* client, const char* topic, const char* payload,
                            int length, int qos, int retain) {
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    if (client->destroyed) throw std::runtime_error("publish used destroyed MQTT client");
    if (qos != 0 || retain != 0) throw std::runtime_error("command direct publish must be QoS 0 non-retained");
    const mqtt_host::Publication item{topic, std::string(payload, length), client->id,
                                      client->credential_revision, in_sdk_callback, false, client->transport_epoch};
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        direct_publish_attempts.push_back(item);
    }
    {
        std::unique_lock<std::mutex> lock(direct_mutex);
        if (direct_paused) {
            direct_entered = true;
            direct_changed.notify_all();
            direct_changed.wait(lock, []() { return !direct_paused; });
            direct_entered = false;
        }
    }
    if (fail_next_direct_publish.exchange(false)) {
        esp_mqtt_event_t disconnected;
        disconnected.client = client;
        disconnected.event_id = MQTT_EVENT_DISCONNECTED;
        PostNativeAndRun(disconnected);
        return -1;
    }
    if (!client->connected) return -1;
    std::lock_guard<std::mutex> state_lock(state_mutex);
    publications.push_back(item);
    wire_publications.push_back(item);
    return 0;
}
int esp_mqtt_dispatch_custom_event(HostMqttClient* client, esp_mqtt_event_t* event) {
    // 故意不取 api_mutex；真实 SDK 这里只投递事件，不能同步调用回调。
    std::lock_guard<std::mutex> lock(client->events_mutex);
    if (fail_next_custom_event.exchange(false)) return ESP_FAIL;
    if (!client->running || client->destroyed) return ESP_FAIL;
    auto& destination = client->legacy_shared_events ? client->events : client->custom_events;
    const size_t capacity = 1;
    if (destination.size() >= capacity) return ESP_FAIL;
    destination.push_back(*event);
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
    ResetDiagnosticLogs();
    before_depth_sample = nullptr;
    before_depth_context = nullptr;
    JoinWorkers();
    std::lock_guard<std::mutex> lock(state_mutex);
    clients.clear();
    publications.clear();
    wire_publications.clear();
    direct_publish_attempts.clear();
    stored_config = {};
    tls_uri.clear(); tls_certificate = nullptr; tls_common_name = nullptr;
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
    hold_wire = false;
    fail_next_direct_publish = false;
    fail_next_custom_event = false;
    fail_next_custom_transfer = false;
    queued_sequence = 0;
    completed_sequence = 0;
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
BrokerTlsSnapshot BrokerTls() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return {tls_uri, tls_certificate == nullptr ? "" : tls_certificate,
                    tls_common_name == nullptr ? "" : tls_common_name};
}
HostMqttClient* CurrentClient() { return current_client; }
void Deliver(esp_mqtt_event_t event) {
    if (event.client == nullptr) event.client = CurrentClient();
    if (event.event_id != MQTT_USER_EVENT) {
        PostNativeAndRun(event);
        return;
    }
    std::lock_guard<std::recursive_mutex> api_lock(event.client->api_mutex);
    while (RunOneCustomEvent(event.client)) {}
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
void HoldWire(bool hold) {
    hold_wire = hold;
    if (hold) return;
    auto* client = CurrentClient();
    if (client == nullptr) return;
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    FlushOutbox(client);
}
void FailNextDirectPublishWithDisconnect() { fail_next_direct_publish = true; }
void FailNextCustomEvent() { fail_next_custom_event = true; }
void FailNextCustomTransfer() { fail_next_custom_transfer = true; }
bool RunOneSdkEvent() {
    auto* client = CurrentClient();
    std::lock_guard<std::recursive_mutex> api_lock(client->api_mutex);
    return RunOneCustomEvent(client);
}
void PauseDirectPublish(bool pause) {
    std::lock_guard<std::mutex> lock(direct_mutex);
    direct_paused = pause;
    direct_changed.notify_all();
}
bool WaitDirectPublishEntered() {
    std::unique_lock<std::mutex> lock(direct_mutex);
    return direct_changed.wait_for(lock, std::chrono::seconds(3), []() { return direct_entered; });
}
size_t PendingUserEvents() {
    auto* client = CurrentClient();
    std::lock_guard<std::mutex> lock(client->events_mutex);
    return client->custom_events.size() + std::count_if(client->events.begin(), client->events.end(),
        [](const auto& event) { return event.event_id == MQTT_USER_EVENT; });
}
void UseLegacySharedEventQueue() {
    auto* client = CurrentClient();
    std::lock_guard<std::recursive_mutex> api_lock(client->api_mutex);
    std::lock_guard<std::mutex> lock(client->events_mutex);
    if (!client->events.empty() || !client->custom_events.empty())
        throw std::runtime_error("cannot change event queue model while events are pending");
    client->legacy_shared_events = true;
}
unsigned DroppedNativeEvents() {
    auto* client = CurrentClient();
    std::lock_guard<std::mutex> lock(client->events_mutex);
    return client->dropped_native_events;
}
size_t PendingNativeEvents() {
    auto* client = CurrentClient();
    std::lock_guard<std::mutex> lock(client->events_mutex);
    return client->events.size();
}
std::vector<Publication> Publications() { std::lock_guard<std::mutex> lock(state_mutex); return publications; }
std::vector<Publication> WirePublications() { std::lock_guard<std::mutex> lock(state_mutex); return wire_publications; }
std::vector<Publication> DirectPublishAttempts() { std::lock_guard<std::mutex> lock(state_mutex); return direct_publish_attempts; }
std::vector<Publication> QueuedPublications() {
    auto* client = CurrentClient();
    if (client == nullptr) return {};
    std::lock_guard<std::recursive_mutex> lock(client->api_mutex);
    return {client->outbox.begin(), client->outbox.end()};
}
uint64_t LastQueuedMessage() { return queued_sequence; }
bool WaitWorkerProcessed(uint64_t sequence) {
    return WaitUntil([=]() { return completed_sequence.load() >= sequence; });
}
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
QueueSnapshot ReadQueue(QueueHandle_t queue) {
    std::lock_guard<std::mutex> lock(queue->mutex);
    return {queue->capacity, queue->items.size(), queue->send_attempts,
            queue->send_accepted, queue->last_send_wait};
}
void BeforeNextQueueDepthSample(void (*callback)(QueueHandle_t, void*), void* context) {
    before_depth_sample = callback;
    before_depth_context = context;
}
}
