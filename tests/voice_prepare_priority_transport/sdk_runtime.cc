#include "sdk_runtime.h"
#include "transport_sdk.h"
#include "phone_os/device_cloud_config.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

struct PrepareSemaphore { unsigned depth=0; bool recursive=false; };
namespace {
unsigned creation=0, fail_creation=0, live_semaphores=0, critical_depth=0, cloud_calls=0, cloud_returns=0;
bool fail_open_take=false;
PrepareSemaphore* open_mutex=nullptr;
std::vector<bool> priority_open_states;
std::vector<unsigned> cloud_returns_at_priority_reads;
[[noreturn]] void Fail(const char* message) { std::fprintf(stderr,"HOST_CONTRACT: %s\n",message); std::abort(); }
void Check(bool value,const char* message) { if(!value) Fail(message); }
SemaphoreHandle_t Create(bool recursive) {
    if(++creation==fail_creation) return nullptr;
    auto* value=new PrepareSemaphore{0,recursive}; ++live_semaphores;
    if(creation==3) open_mutex=value;
    return value;
}
}
namespace prepare_host {
std::function<void()> after_cloud_return;
void Reset(unsigned fail) {
    Check(live_semaphores==0,"previous transport still alive");
    creation=0; fail_creation=fail; open_mutex=nullptr; fail_open_take=false; ClearTrace();
}
void ClearTrace() { priority_open_states.clear(); cloud_returns_at_priority_reads.clear(); cloud_calls=0; cloud_returns=0; after_cloud_return={}; }
void FailOpenTakeOnce() { fail_open_take=true; }
bool OpenHeld() { return open_mutex && open_mutex->depth!=0; }
unsigned LiveSemaphores() { return live_semaphores; }
unsigned CloudCalls() { return cloud_calls; }
const std::vector<bool>& PriorityOpenStates() { return priority_open_states; }
const std::vector<unsigned>& CloudReturnsAtPriorityReads() { return cloud_returns_at_priority_reads; }
}
void prepare_enter_critical(portMUX_TYPE* mux) {
    Check(mux && mux->locked==0,"nested observer lock"); mux->locked=1; ++critical_depth;
}
void prepare_exit_critical(portMUX_TYPE* mux) {
    Check(mux && mux->locked==1 && critical_depth==1,"observer unlock mismatch"); mux->locked=0; --critical_depth;
}
TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void*>(1); }
int eTaskGetState(TaskHandle_t) { Fail("Prepare must not query a cleanup worker"); }
UBaseType_t uxTaskPriorityGet(TaskHandle_t handle) {
    Check(handle==nullptr,"priority query must be self/null");
    Check(critical_depth==0,"priority getter under observer lock");
    priority_open_states.push_back(prepare_host::OpenHeld());
    cloud_returns_at_priority_reads.push_back(cloud_returns); return 4;
}
const char* pcTaskGetName(TaskHandle_t handle) { Check(handle==nullptr,"name query must be self/null"); return "wake_notify"; }
SemaphoreHandle_t xSemaphoreCreateMutex() { return Create(false); }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return Create(true); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t value,TickType_t) {
    if(value==open_mutex && fail_open_take) { fail_open_take=false; return pdFALSE; }
    Check(value && (value->recursive || value->depth==0),"unexpected semaphore contention"); ++value->depth; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t value) { Check(value && value->depth,"unheld semaphore give"); --value->depth; return pdTRUE; }
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t v,TickType_t t) { Check(v && v->recursive,"not recursive");return xSemaphoreTake(v,t); }
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t v) { return xSemaphoreGive(v); }
void vSemaphoreDelete(SemaphoreHandle_t value) {
    Check(value && value->depth==0,"delete held semaphore"); if(value==open_mutex) open_mutex=nullptr;delete value;--live_semaphores;
}
EventGroupHandle_t xEventGroupCreate() { return new EventBits_t(0); }
void vEventGroupDelete(EventGroupHandle_t v) { delete v; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t v,EventBits_t bits) { return *v|=bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t v,EventBits_t bits) { auto old=*v;*v&=~bits;return old; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t,EventBits_t,BaseType_t,BaseType_t,TickType_t) { Fail("Prepare must not wait for websocket events"); }
BaseType_t xTaskCreateWithCaps(TaskFunction_t,const char*,unsigned,void*,UBaseType_t,TaskHandle_t*,UBaseType_t) { Fail("Prepare must not create a websocket worker"); }
void vTaskSuspend(TaskHandle_t) { Fail("Prepare must not suspend task"); }
void vTaskDeleteWithCaps(TaskHandle_t) { Fail("Prepare must not delete task"); }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { Fail("Prepare must not query stack watermark"); }
size_t heap_caps_get_free_size(unsigned) { Fail("Prepare must not query physical heap"); }
size_t heap_caps_get_minimum_free_size(unsigned) { Fail("Prepare must not query physical heap"); }
size_t heap_caps_get_largest_free_block(unsigned) { Fail("Prepare must not query physical heap"); }
esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t*) { Fail("unexpected websocket init"); }
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t) { Fail("unexpected websocket start"); }
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t) { Fail("unexpected websocket stop"); }
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t) { Fail("unexpected websocket destroy"); }
bool esp_websocket_client_is_connected(esp_websocket_client_handle_t) { Fail("unexpected websocket query"); }
int esp_websocket_client_send_bin(esp_websocket_client_handle_t,const char*,int,TickType_t) { Fail("unexpected websocket send"); }
int esp_websocket_client_send_text(esp_websocket_client_handle_t,const char*,int,TickType_t) { Fail("unexpected websocket send"); }
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t,int,esp_event_handler_t,void*) { Fail("unexpected websocket register"); }
esp_err_t esp_websocket_unregister_events(esp_websocket_client_handle_t,int,esp_event_handler_t) { Fail("unexpected websocket unregister"); }

extern "C" bool __real__ZN7rodakos24DeviceCloudConfigService18PrepareVoiceConfigERNS_17DeviceCloudConfigERKSt8functionIFbvEEPNS_19CloudDiagnosticCodeE(
    rodakos::DeviceCloudConfigService*,rodakos::DeviceCloudConfig&,const std::function<bool()>&,rodakos::CloudDiagnosticCode*);
extern "C" bool __wrap__ZN7rodakos24DeviceCloudConfigService18PrepareVoiceConfigERNS_17DeviceCloudConfigERKSt8functionIFbvEEPNS_19CloudDiagnosticCodeE(
    rodakos::DeviceCloudConfigService* self,rodakos::DeviceCloudConfig& config,const std::function<bool()>& guard,rodakos::CloudDiagnosticCode* failure) {
    ++cloud_calls; Check(prepare_host::OpenHeld(),"Cloud preparation must hold transport open mutex");
    const bool result=__real__ZN7rodakos24DeviceCloudConfigService18PrepareVoiceConfigERNS_17DeviceCloudConfigERKSt8functionIFbvEEPNS_19CloudDiagnosticCodeE(self,config,guard,failure);
    ++cloud_returns;
    if(prepare_host::after_cloud_return) prepare_host::after_cloud_return();
    return result;
}
