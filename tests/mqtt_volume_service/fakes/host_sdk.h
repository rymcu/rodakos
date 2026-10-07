#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_FAIL = -1;
constexpr int ESP_ERR_INVALID_STATE = -2;
constexpr int ESP_EVENT_ANY_ID = -1;
inline const char* esp_err_to_name(int) { return "host error"; }
using esp_event_base_t = const char*;
using esp_event_handler_instance_t = void*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
inline constexpr const char* IP_EVENT = "ip";
constexpr int IP_EVENT_STA_GOT_IP = 1;
inline int esp_event_handler_instance_register(esp_event_base_t, int, esp_event_handler_t,
                                               void*, void** handle) {
    *handle = reinterpret_cast<void*>(1);
    return ESP_OK;
}
inline int esp_event_handler_instance_unregister(esp_event_base_t, int, void*) { return ESP_OK; }

#ifdef RODAK_IDENTITY_WITH_RETIREMENT
// Only the real wake/MQTT integration fixture opts in. Keep the ordinary MQTT
// worker model under distinct symbols; WithCaps uses the real IDF host fixture.
#include "../../task_retirement/fakes/freertos/FreeRTOS.h"
#include "../../task_retirement/fakes/freertos/task.h"
int mqtt_host_task_create(TaskFunction_t, const char*, uint32_t, void*, uint32_t, TaskHandle_t*);
void mqtt_host_task_delay(TickType_t);
inline void mqtt_host_task_delete(TaskHandle_t) {}
#define xTaskCreate mqtt_host_task_create
#define vTaskDelay mqtt_host_task_delay
#define vTaskDelete mqtt_host_task_delete
#else
using BaseType_t = int;
using UBaseType_t = unsigned;
using StackType_t = uint32_t;
using TickType_t = uint32_t;
constexpr int pdTRUE = 1;
constexpr int pdFALSE = 0;
constexpr int pdPASS = 1;
constexpr int pdFAIL = 0;
constexpr TickType_t portMAX_DELAY = 0xffffffffu;
constexpr TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
int xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, uint32_t, TaskHandle_t*);
void vTaskDelay(TickType_t ticks);
inline void vTaskDelete(TaskHandle_t) {}
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
#endif
inline TickType_t xTaskGetTickCount() {
    return static_cast<TickType_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct HostSemaphore {
    std::recursive_mutex mutex;
    std::condition_variable_any changed;
    bool binary = false;
    bool dynamic = false;
    bool available = false;
};
using StaticSemaphore_t = HostSemaphore;
using SemaphoreHandle_t = HostSemaphore*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    auto* value = new HostSemaphore;
    value->dynamic = true;
    return value;
}
inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* value) {
    value->binary = true;
    value->available = false;
    return value;
}
inline int xSemaphoreTake(SemaphoreHandle_t value, TickType_t ticks) {
    if (!value->binary) { value->mutex.lock(); return pdTRUE; }
    std::unique_lock<std::recursive_mutex> lock(value->mutex);
    if (ticks == portMAX_DELAY) value->changed.wait(lock, [&]() { return value->available; });
    else if (!value->changed.wait_for(lock, std::chrono::milliseconds(ticks),
                                     [&]() { return value->available; })) return pdFALSE;
    value->available = false;
    return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t value) {
    if (!value->binary) { value->mutex.unlock(); return pdTRUE; }
    std::lock_guard<std::recursive_mutex> lock(value->mutex);
    value->available = true;
    value->changed.notify_all();
    return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t value) { if (value->dynamic) delete value; }

struct HostQueue {
    std::mutex mutex;
    std::condition_variable changed;
    size_t capacity;
    struct Entry { void* value; uint64_t sequence; };
    std::deque<Entry> items;
    size_t send_attempts = 0;
    size_t send_accepted = 0;
    TickType_t last_send_wait = 0;
};
using QueueHandle_t = HostQueue*;
QueueHandle_t xQueueCreate(unsigned capacity, unsigned);
int xQueueSend(QueueHandle_t queue, const void* item, TickType_t);
int xQueueReceive(QueueHandle_t queue, void* output, TickType_t wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
inline void vQueueDelete(QueueHandle_t value) { delete value; }

struct HostTimer { void* id; };
using TimerHandle_t = HostTimer*;
using TimerCallbackFunction_t = void (*)(TimerHandle_t);
inline TimerHandle_t xTimerCreate(const char*, TickType_t, int, void* id, TimerCallbackFunction_t) {
    return new HostTimer{id};
}
inline int xTimerStart(TimerHandle_t, TickType_t) { return pdPASS; }
inline int xTimerStop(TimerHandle_t, TickType_t) { return pdPASS; }
inline int xTimerDelete(TimerHandle_t timer, TickType_t) { delete timer; return pdPASS; }
inline void* pvTimerGetTimerID(TimerHandle_t timer) { return timer->id; }
inline int xTimerPendFunctionCall(void (*callback)(void*, uint32_t), void* context,
                                 uint32_t value, TickType_t) {
    callback(context, value);
    return pdPASS;
}

struct esp_app_desc_t { char version[32] = "mqtt-volume-host"; };
inline const esp_app_desc_t* esp_app_get_description() { static esp_app_desc_t app; return &app; }
struct esp_partition_t { const char* label = "ota_0"; };
inline const esp_partition_t* esp_ota_get_running_partition() { static esp_partition_t part; return &part; }
struct wifi_ap_record_t { int rssi = -40; };
inline int esp_wifi_sta_get_ap_info(wifi_ap_record_t*) { return ESP_OK; }
inline unsigned esp_get_free_heap_size() { return 1024 * 1024; }
inline unsigned esp_get_minimum_free_heap_size() { return 1024 * 1024; }
namespace mqtt_host { inline thread_local std::atomic<bool>* clock_read_observed = nullptr; }
inline int64_t esp_timer_get_time() {
    const int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (mqtt_host::clock_read_observed) mqtt_host::clock_read_observed->store(true);
    return now_us;
}
void esp_restart();
constexpr unsigned MALLOC_CAP_INTERNAL = 1;
constexpr unsigned MALLOC_CAP_SPIRAM = 2;
constexpr unsigned MALLOC_CAP_8BIT = 4;
constexpr unsigned MALLOC_CAP_DMA = 8;
constexpr unsigned MALLOC_CAP_DEFAULT = 16;
inline unsigned heap_caps_get_free_size(unsigned caps) {
    return caps == MALLOC_CAP_DEFAULT ? 131072 : 1024 * 1024;
}
inline unsigned heap_caps_get_minimum_free_size(unsigned) { return 1024 * 1024; }
inline unsigned heap_caps_get_largest_free_block(unsigned caps) {
    return caps == MALLOC_CAP_DEFAULT ? 16384 : 1024 * 1024;
}

struct HostMqttClient;
using esp_mqtt_client_handle_t = HostMqttClient*;
enum esp_mqtt_event_id_t {
    MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_ERROR, MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DELETED, MQTT_EVENT_DATA, MQTT_USER_EVENT
};
constexpr int MQTT_ERROR_TYPE_TCP_TRANSPORT = 1;
constexpr int MQTT_ERROR_TYPE_CONNECTION_REFUSED = 2;
constexpr int MQTT_CONNECTION_REFUSE_BAD_USERNAME = 4;
constexpr int MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED = 5;
struct esp_mqtt_error_codes_t { int error_type = 0; int connect_return_code = 0; };
struct esp_mqtt_event_t {
    esp_mqtt_client_handle_t client = nullptr;
    esp_mqtt_event_id_t event_id = MQTT_EVENT_CONNECTED;
    esp_mqtt_error_codes_t* error_handle = nullptr;
    int msg_id = 0;
    int current_data_offset = 0;
    int data_len = 0;
    int total_data_len = 0;
    const char* topic = nullptr;
    int topic_len = 0;
    const char* data = nullptr;
};
using esp_mqtt_event_handle_t = esp_mqtt_event_t*;
struct esp_mqtt_client_config_t {
    struct {
        struct { const char* uri = nullptr; } address;
        struct {
            const char* certificate = nullptr;
            size_t certificate_len = 0;
            const char* common_name = nullptr;
        } verification;
    } broker;
    struct {
        const char* client_id = nullptr;
        const char* username = nullptr;
        struct { const char* password = nullptr; } authentication;
    } credentials;
    struct { int keepalive = 0; } session;
    struct { int reconnect_timeout_ms = 0; int timeout_ms = 0; } network;
    struct { int stack_size = 0; } task;
};
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t*);
int esp_mqtt_client_register_event(esp_mqtt_client_handle_t, esp_mqtt_event_id_t,
                                   esp_event_handler_t, void*);
int esp_mqtt_client_start(esp_mqtt_client_handle_t);
int esp_mqtt_client_stop(esp_mqtt_client_handle_t);
int esp_mqtt_client_destroy(esp_mqtt_client_handle_t);
int esp_mqtt_client_reconnect(esp_mqtt_client_handle_t);
int esp_mqtt_set_config(esp_mqtt_client_handle_t, const esp_mqtt_client_config_t*);
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t);
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char*, int);
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t, const char*, const char*, int, int, int, bool);
int esp_mqtt_client_publish(esp_mqtt_client_handle_t, const char*, const char*, int, int, int);
int esp_mqtt_dispatch_custom_event(esp_mqtt_client_handle_t, esp_mqtt_event_t*);
