#pragma once
#include "host_sdk.h"
#include <cstddef>
#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned;
using TickType_t = unsigned;
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
#define ESP_LOGD(...) ((void)0)
constexpr int eSuspended=3;
int eTaskGetState(TaskHandle_t);
constexpr int pdFALSE = 0, pdTRUE = 1, pdFAIL = 0, pdPASS = 1;
constexpr TickType_t portMAX_DELAY = ~TickType_t{0};
struct portMUX_TYPE { unsigned locked; };
#define portMUX_INITIALIZER_UNLOCKED {0}
void prepare_enter_critical(portMUX_TYPE*);
void prepare_exit_critical(portMUX_TYPE*);
#define portENTER_CRITICAL(mux) prepare_enter_critical(mux)
#define portEXIT_CRITICAL(mux) prepare_exit_critical(mux)
TaskHandle_t xTaskGetCurrentTaskHandle();
UBaseType_t uxTaskPriorityGet(TaskHandle_t);
const char* pcTaskGetName(TaskHandle_t);
BaseType_t xTaskCreateWithCaps(TaskFunction_t, const char*, unsigned, void*, UBaseType_t,
                               TaskHandle_t*, UBaseType_t);
void vTaskSuspend(TaskHandle_t);
void vTaskDeleteWithCaps(TaskHandle_t);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t);

struct PrepareSemaphore;
using SemaphoreHandle_t = PrepareSemaphore*;
SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
using EventBits_t = unsigned;
using EventGroupHandle_t = EventBits_t*;
constexpr EventBits_t BIT0=1, BIT1=2, BIT2=4, BIT3=8;
EventGroupHandle_t xEventGroupCreate();
void vEventGroupDelete(EventGroupHandle_t);
EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t, EventBits_t, BaseType_t, BaseType_t, TickType_t);
constexpr unsigned MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_SPIRAM=4;
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_minimum_free_size(unsigned);
size_t heap_caps_get_largest_free_block(unsigned);

using esp_event_base_t = const char*;
using esp_websocket_client_handle_t = void*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
using esp_websocket_error_type_t = int;
constexpr int WEBSOCKET_EVENT_ANY=-1, WEBSOCKET_EVENT_CONNECTED=1, WEBSOCKET_EVENT_DISCONNECTED=2,
    WEBSOCKET_EVENT_CLOSED=3, WEBSOCKET_EVENT_ERROR=4, WEBSOCKET_EVENT_DATA=5;
constexpr int WEBSOCKET_ERROR_TYPE_NONE=0, WEBSOCKET_ERROR_TYPE_PONG_TIMEOUT=1;
constexpr int WS_TRANSPORT_OPCODES_CONT=0, WS_TRANSPORT_OPCODES_TEXT=1, WS_TRANSPORT_OPCODES_BINARY=2;
struct esp_websocket_client_config_t {
    const char* uri=nullptr; const char* headers=nullptr; const char* task_name=nullptr;
    const char* cert_pem=nullptr; const char* cert_common_name=nullptr;
    int buffer_size=0, network_timeout_ms=0, reconnect_timeout_ms=0, pingpong_timeout_sec=0, task_stack=0;
    size_t cert_len=0; bool disable_auto_reconnect=false;
    int (*crt_bundle_attach)(void*)=nullptr;
};
struct esp_websocket_event_data_t {
    esp_websocket_client_handle_t client=nullptr;
    const char* data_ptr=nullptr;
    int data_len=0, payload_len=0, payload_offset=0, op_code=0, close_status_code=0;
    bool fin=false;
    struct { int esp_ws_handshake_status_code=0; esp_websocket_error_type_t error_type=0; } error_handle;
};
esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t*);
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t);
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t);
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t);
bool esp_websocket_client_is_connected(esp_websocket_client_handle_t);
int esp_websocket_client_send_bin(esp_websocket_client_handle_t, const char*, int, TickType_t);
int esp_websocket_client_send_text(esp_websocket_client_handle_t, const char*, int, TickType_t);
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t, int, esp_event_handler_t, void*);
esp_err_t esp_websocket_unregister_events(esp_websocket_client_handle_t, int, esp_event_handler_t);
