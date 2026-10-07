#pragma once
#include "host_sdk.h"
#include "phone_os/device_cloud_config.h"

namespace mqtt_host {
struct BrokerTlsSnapshot {
    std::string uri;
    std::string certificate;
    std::string common_name;
};
BrokerTlsSnapshot BrokerTls();
struct Publication {
    std::string topic;
    std::string payload;
    unsigned client_id;
    unsigned credential_revision;
    bool in_sdk_callback;
    bool via_outbox = true;
    unsigned transport_epoch = 0;
};
void Reset();
void JoinWorkers();
void SetConfig(const rodakos::DeviceCloudConfig& config);
rodakos::DeviceCloudConfig Config();
esp_mqtt_client_handle_t CurrentClient();
void Deliver(esp_mqtt_event_t event);
void Fragment(const std::string& topic, const std::string& bytes, int offset, int total);
void Message(const std::string& topic, const std::string& payload, bool fragmented = false);
void Disconnect();
void Connect();
void RejectCredentials();
void HoldUserEvents(bool hold);
size_t PendingUserEvents();
void UseLegacySharedEventQueue();
unsigned DroppedNativeEvents();
size_t PendingNativeEvents();
void HoldWire(bool hold);
void FailNextDirectPublishWithDisconnect();
void FailNextCustomEvent();
void FailNextCustomTransfer();
bool RunOneSdkEvent();
void PauseDirectPublish(bool pause);
bool WaitDirectPublishEntered();
std::vector<Publication> Publications();
std::vector<Publication> WirePublications();
std::vector<Publication> QueuedPublications();
std::vector<Publication> DirectPublishAttempts();
uint64_t LastQueuedMessage();
bool WaitWorkerProcessed(uint64_t sequence);
size_t ReceiptCount();
unsigned CredentialRevision();
unsigned Restarts();
void PauseDequeue(bool pause);
bool WaitDequeued();
bool WaitUntil(const std::function<bool()>& predicate);
struct QueueSnapshot {
    size_t capacity;
    size_t depth;
    size_t send_attempts;
    size_t send_accepted;
    TickType_t last_send_wait;
};
QueueSnapshot ReadQueue(QueueHandle_t queue);
void BeforeNextQueueDepthSample(void (*callback)(QueueHandle_t, void*), void* context);
}
