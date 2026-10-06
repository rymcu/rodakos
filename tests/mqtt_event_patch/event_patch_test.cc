#include <cassert>
#include <cstddef>
#include <deque>
#include <functional>
#include <iostream>

// These are event/FreeRTOS fakes, not a compiled ESP-IDF transport stack.
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_FAIL = -1;
constexpr int ESP_ERR_INVALID_ARG = -2;
constexpr int ESP_ERR_TIMEOUT = -3;
constexpr int ESP_ERR_NO_MEM = -4;
constexpr int pdTRUE = 1;
constexpr int portMAX_DELAY = -1;
constexpr int MQTT_EVENTS = 1;
constexpr int MQTT_USER_EVENT = 7;
constexpr int MQTT_PROTOCOL_V_5 = 5;
#define MQTT_EVENT_QUEUE_SIZE 1
#define MQTT_SUPPORTED_FEATURE_EVENT_LOOP
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
struct esp_mqtt_client;
using esp_mqtt_client_handle_t = esp_mqtt_client*;
struct esp_mqtt_event_t {
    int event_id = 0;
    esp_mqtt_client_handle_t client = nullptr;
    int protocol_ver = 3;
};
struct Queue {
    std::deque<esp_mqtt_event_t> events;
    std::size_t capacity = 1;
};
struct EventLoop {
    Queue native;
    std::function<void(const esp_mqtt_event_t&)> callback;
    std::function<void()> allocation_failure_hook;
    int fail_posts = 0;
    int capacity_failures = 0;
};
struct Config { EventLoop* event_loop_handle; };
struct esp_mqtt_client {
    Config* config;
    Queue* rodak_custom_events;
    esp_mqtt_event_t event;
    struct { struct { struct { int protocol_ver = 3; } information; } connection; } mqtt_state;
};
static int xQueueSend(Queue* queue, const esp_mqtt_event_t* event, int wait) {
    assert(wait == 0);
    if (queue->events.size() >= queue->capacity) return 0;
    queue->events.push_back(*event);
    return pdTRUE;
}
static int xQueueSendToFront(Queue* queue, const esp_mqtt_event_t* event, int wait) {
    assert(wait == 0);
    if (queue->events.size() >= queue->capacity) return 0;
    queue->events.push_front(*event);
    return pdTRUE;
}
static int xQueueReceive(Queue* queue, esp_mqtt_event_t* event, int wait) {
    assert(wait == 0);
    if (queue->events.empty()) return 0;
    *event = queue->events.front();
    queue->events.pop_front();
    return pdTRUE;
}
static void xQueueReset(Queue* queue) { queue->events.clear(); }
static std::size_t uxQueueMessagesWaiting(Queue* queue) { return queue->events.size(); }
static esp_err_t esp_event_post_to(EventLoop* loop, int, int id, const void* data,
                                   std::size_t bytes, int) {
    assert(bytes == sizeof(esp_mqtt_event_t));
    if (loop->fail_posts > 0) {
        --loop->fail_posts;
        if (loop->allocation_failure_hook) loop->allocation_failure_hook();
        return ESP_ERR_NO_MEM;
    }
    auto event = *static_cast<const esp_mqtt_event_t*>(data);
    event.event_id = id;
    if (xQueueSend(&loop->native, &event, 0) != pdTRUE) {
        ++loop->capacity_failures;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
static esp_err_t esp_event_loop_run(EventLoop* loop, int timeout) {
    assert(timeout == 0);
    esp_mqtt_event_t event;
    if (xQueueReceive(&loop->native, &event, 0) == pdTRUE) loop->callback(event);
    return ESP_OK;
}

#include "mqtt_event_functions.inc"

struct Fixture {
    EventLoop loop;
    Queue custom;
    Config config{&loop};
    esp_mqtt_client client{&config, &custom, {}, {}};
    esp_mqtt_event_t wake{MQTT_USER_EVENT, &client, 3};
    Fixture() { loop.callback = [](const auto&) {}; }
    void Native(int id) { client.event.event_id = id; assert(esp_mqtt_dispatch_event(&client) == ESP_OK); }
};

static void CheckLifecycleIsolation(bool legacy) {
    Fixture fixture;
    bool observed_connected = true;
    bool transport_connected = true;
    int epoch = 1;
    int stale_writes = 0;
    fixture.loop.callback = [&](const auto& event) {
        if (event.event_id == 1) { observed_connected = false; ++epoch; }
        else if (event.event_id == 2) { observed_connected = true; ++epoch; }
        else if (observed_connected && epoch == 1 && transport_connected) ++stale_writes;
    };
    auto dispatch = legacy ? upstream_dispatch_custom_event : esp_mqtt_dispatch_custom_event;
    assert(dispatch(&fixture.client, &fixture.wake) == ESP_OK);
    transport_connected = false;
    fixture.Native(1);
    if (!legacy) {
        assert(!observed_connected);
        run_event_loop(&fixture.client);
    }
    assert(dispatch(&fixture.client, &fixture.wake) == ESP_OK);
    transport_connected = true;
    fixture.Native(2);
    if (!legacy) run_event_loop(&fixture.client);
    assert(stale_writes == (legacy ? 1 : 0));
    assert(fixture.loop.capacity_failures == (legacy ? 2 : 0));
}

int main() {
    CheckLifecycleIsolation(true);   // Upstream negative control reproduces both dropped events.
    CheckLifecycleIsolation(false);  // The same one-slot native queue now preserves both.
    {
        Fixture fixture;
        int callbacks = 0;
        fixture.loop.callback = [&](const auto&) { ++callbacks; };
        assert(esp_mqtt_dispatch_custom_event(nullptr, &fixture.wake) == ESP_ERR_INVALID_ARG);
        assert(esp_mqtt_dispatch_custom_event(&fixture.client, nullptr) == ESP_ERR_INVALID_ARG);
        assert(esp_mqtt_dispatch_custom_event(&fixture.client, &fixture.wake) == ESP_OK);
        assert(esp_mqtt_dispatch_custom_event(&fixture.client, &fixture.wake) == ESP_ERR_TIMEOUT);
        assert(fixture.loop.native.events.empty());
        assert(max_poll_timeout(&fixture.client, 1000) == 10);
        run_event_loop(&fixture.client);
        assert(callbacks == 1 && max_poll_timeout(&fixture.client, 1000) == 1000);
    }
    {
        Fixture fixture;
        bool disconnected = false;
        fixture.loop.callback = [&](const auto& event) {
            if (event.event_id == MQTT_USER_EVENT) {
                fixture.Native(1); // Direct publish can synchronously reenter on a write failure.
                assert(disconnected);
            } else { disconnected = true; }
        };
        assert(esp_mqtt_dispatch_custom_event(&fixture.client, &fixture.wake) == ESP_OK);
        run_event_loop(&fixture.client);
        assert(disconnected && fixture.loop.capacity_failures == 0);
    }
    for (bool concurrent_producer : {false, true}) {
        Fixture fixture;
        int callbacks = 0;
        fixture.loop.callback = [&](const auto&) { ++callbacks; };
        fixture.loop.fail_posts = 2;
        if (concurrent_producer) fixture.loop.allocation_failure_hook = [&]() {
            assert(esp_mqtt_dispatch_custom_event(&fixture.client, &fixture.wake) == ESP_OK);
        };
        assert(esp_mqtt_dispatch_custom_event(&fixture.client, &fixture.wake) == ESP_OK);
        run_event_loop(&fixture.client);
        assert(callbacks == 0 && fixture.custom.events.size() == 1);
        run_event_loop(&fixture.client);
        assert(callbacks == 0 && fixture.custom.events.size() == 1);
        run_event_loop(&fixture.client);
        assert(callbacks == 1 && fixture.custom.events.empty());
    }
    {
        Fixture fixture;
        fixture.loop.callback = [&](const auto&) {
            assert(esp_mqtt_dispatch_custom_event(&fixture.client, &fixture.wake) == ESP_OK);
        };
        assert(esp_mqtt_dispatch_custom_event(&fixture.client, &fixture.wake) == ESP_OK);
        run_event_loop(&fixture.client); // Requeued callback is deferred to the next iteration.
        assert(fixture.custom.events.size() == 1);
        rodak_mqtt_reset_custom_events(&fixture.client);
        assert(fixture.custom.events.empty());
    }
    std::cout << "7 MQTT event overlay scenarios passed (including upstream negative control)\n";
}
