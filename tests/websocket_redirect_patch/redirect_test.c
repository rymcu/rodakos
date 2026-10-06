// Exercise unmodified production task/abort/stop bodies with deterministic transport/RTOS fakes.
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int esp_err_t;
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef uintptr_t TaskHandle_t;
typedef void *esp_event_loop_handle_t;
typedef void *esp_transport_handle_t;
typedef void *esp_transport_list_handle_t;
typedef void *EventGroupHandle_t;
typedef void *SemaphoreHandle_t;
typedef int ws_transport_opcodes_t;
typedef int esp_transport_keep_alive_t;
typedef struct esp_websocket_client *esp_websocket_client_handle_t;
typedef struct { int last_error, esp_tls_error_code, esp_tls_flags; } tls_error_t;
typedef tls_error_t *esp_tls_error_handle_t;
enum { ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = -2, ESP_ERR_NO_MEM = -3 };
enum { pdPASS = 1, portMAX_DELAY = 1000000 };
enum { STOPPED_BIT = 1, CLOSE_FRAME_SENT_BIT = 2, REQUESTED_STOP_BIT = 4, WAKEUP_BIT = 8 };
enum { WEBSOCKET_EVENT_ERROR, WEBSOCKET_EVENT_CONNECTED, WEBSOCKET_EVENT_DISCONNECTED,
       WEBSOCKET_EVENT_BEFORE_CONNECT, WEBSOCKET_EVENT_BEGIN, WEBSOCKET_EVENT_FINISH,
       WEBSOCKET_EVENT_CLOSED, EVENT_COUNT };
enum { WS_TRANSPORT_OPCODES_NONE, WS_TRANSPORT_OPCODES_PING, WS_TRANSPORT_OPCODES_CLOSE,
       WS_TRANSPORT_OPCODES_FIN = 128 };
#define WS_HTTP_REDIRECT(code) ((code >= 300) && (code < 400))
#define WS_TRANSPORT_REDIRECT_HEADER_SUPPORT 1
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define ESP_WS_CLIENT_STATE_CHECK(tag, client, action) \
    do { if (client->state < WEBSOCKET_STATE_INIT) { action; } } while (0)

#include "ws_types.inc"

static const char *const original_uri = "wss://192.0.2.10:9443/voice/session";
static const char *const original_host = "192.0.2.10";
static const char *const original_name = "rodak-pinned.test";
static struct esp_websocket_client client;
static websocket_config_storage_t config;
static tls_error_t tls_error;
static pthread_mutex_t bits_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bits_changed = PTHREAD_COND_INITIALIZER;
static struct {
    int results[4], statuses[4], result_count, connects, closes, uri_updates, location_reads;
    int events[EVENT_COUNT], error_status, disconnected_status, error_type, disconnected_type;
    int lock_depth, lock_takes, deleted, destroyed, wait_calls, route_changes, errors;
    unsigned bits;
    uint64_t now;
    const char *location;
    bool stop_during_wait, wait_reached, with_tls_error;
    char message[256];
} fake;
static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); ++failures; } } while (0)

static unsigned xEventGroupGetBits(EventGroupHandle_t ignored)
{
    (void)ignored;
    pthread_mutex_lock(&bits_lock);
    unsigned bits = fake.bits;
    pthread_mutex_unlock(&bits_lock);
    return bits;
}
static unsigned xEventGroupSetBits(EventGroupHandle_t ignored, unsigned bits)
{
    (void)ignored;
    pthread_mutex_lock(&bits_lock);
    fake.bits |= bits;
    pthread_cond_broadcast(&bits_changed);
    unsigned result = fake.bits;
    pthread_mutex_unlock(&bits_lock);
    return result;
}
static unsigned xEventGroupClearBits(EventGroupHandle_t ignored, unsigned bits)
{
    (void)ignored;
    pthread_mutex_lock(&bits_lock);
    unsigned result = fake.bits;
    fake.bits &= ~bits;
    pthread_mutex_unlock(&bits_lock);
    return result;
}
static unsigned xEventGroupWaitBits(EventGroupHandle_t ignored, unsigned bits,
                                   bool clear, bool all, TickType_t ticks)
{
    (void)ignored; (void)clear; (void)all;
    pthread_mutex_lock(&bits_lock);
    if (bits == STOPPED_BIT) {
        while (!(fake.bits & STOPPED_BIT)) pthread_cond_wait(&bits_changed, &bits_lock);
    } else {
        ++fake.wait_calls;
        if (fake.stop_during_wait) {
            fake.wait_reached = true;
            pthread_cond_broadcast(&bits_changed);
            while (!(fake.bits & REQUESTED_STOP_BIT)) pthread_cond_wait(&bits_changed, &bits_lock);
        } else {
            fake.now += ticks;
        }
    }
    unsigned result = fake.bits;
    pthread_mutex_unlock(&bits_lock);
    return result;
}
static int xSemaphoreTakeRecursive(SemaphoreHandle_t ignored, int timeout)
{
    (void)ignored; (void)timeout;
    if (++fake.lock_takes > 50) { ++fake.errors; client.run = false; return 0; }
    ++fake.lock_depth;
    return pdPASS;
}
static void xSemaphoreGiveRecursive(SemaphoreHandle_t ignored)
{ (void)ignored; CHECK(fake.lock_depth > 0); --fake.lock_depth; }
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return 2; }
static void vTaskDelete(void *ignored) { (void)ignored; ++fake.deleted; }
static uint64_t _tick_get_ms(void) { return fake.now; }
static esp_transport_handle_t esp_transport_list_get_transport(void *list, const char *scheme)
{ (void)list; (void)scheme; return &fake; }
static int esp_transport_get_default_port(void *transport) { (void)transport; return 443; }
static int esp_transport_close(void *transport) { (void)transport; ++fake.closes; return 0; }
static int esp_transport_connect(void *transport, const char *host, int port, int timeout)
{
    (void)transport; (void)timeout;
    CHECK(fake.lock_depth == 1);
    if (strcmp(host, original_host) || port != 9443 ||
        strcmp(config.headers, "Authorization: Bearer TEST-ONLY\r\n") ||
        strcmp(config.cert_common_name, original_name) ||
        strcmp(config.cert, "TEST-ONLY-PIN") || config.skip_cert_common_name_check) ++fake.route_changes;
    int index = fake.connects++;
    if (index >= fake.result_count) { ++fake.errors; return -1; }
    return fake.results[index];
}
static int esp_transport_ws_get_upgrade_request_status(void *transport)
{ (void)transport; return fake.statuses[fake.connects - 1]; }
static esp_tls_error_handle_t esp_transport_get_error_handle(void *transport)
{ (void)transport; return fake.with_tls_error ? &tls_error : NULL; }
static const char *esp_err_to_name(int error) { (void)error; return "test-error"; }
#ifdef WS_USE_UPSTREAM
static const char *esp_transport_ws_get_redir_uri(void *transport)
{ (void)transport; ++fake.location_reads; return fake.location; }
static esp_err_t esp_websocket_client_set_uri(esp_websocket_client_handle_t ws, const char *uri)
{
    ++fake.uri_updates;
    if (strncmp(uri, "wss://", 6)) return ESP_FAIL;
    free(ws->config->host);
    ws->config->host = strdup("redirect.invalid");
    ws->config->port = 443;
    return ESP_OK;
}
#endif
static int esp_transport_poll_read(void *transport, int timeout)
{ (void)transport; (void)timeout; return 0; }
static int esp_transport_ws_send_raw(void *transport, int opcode, const void *data, int len, int timeout)
{ (void)transport; (void)opcode; (void)data; (void)len; (void)timeout; return 0; }
static int esp_transport_ws_poll_connection_closed(void *transport, int timeout)
{ (void)transport; (void)timeout; return 1; }
static int esp_websocket_client_recv(esp_websocket_client_handle_t ws) { (void)ws; return ESP_OK; }
static void esp_event_loop_run(void *event, int ticks) { (void)event; (void)ticks; }
static void destroy_and_free_resources(esp_websocket_client_handle_t ws)
{ (void)ws; ++fake.destroyed; }
static esp_err_t esp_websocket_client_dispatch_event(esp_websocket_client_handle_t ws,
                                                     int event, const char *data, int len)
{
    ++fake.events[event];
    if (event == WEBSOCKET_EVENT_ERROR) {
        fake.error_status = ws->error_handle.esp_ws_handshake_status_code;
        fake.error_type = ws->error_handle.error_type;
        snprintf(fake.message, sizeof(fake.message), "%.*s", len, data);
    }
    if (event == WEBSOCKET_EVENT_DISCONNECTED) {
        fake.disconnected_status = ws->error_handle.esp_ws_handshake_status_code;
        fake.disconnected_type = ws->error_handle.error_type;
    }
    if (event == WEBSOCKET_EVENT_CONNECTED) ws->run = false;
    return ESP_OK;
}

// The reviewed upstream task compares its unsigned tick with a signed timeout.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#ifdef WS_USE_UPSTREAM
#include "ws_upstream_functions.inc"
#else
#include "ws_functions.inc"
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void setup(bool reconnect, int result, int status, const char *location)
{
    memset(&fake, 0, sizeof(fake));
    memset(&client, 0, sizeof(client));
    memset(&config, 0, sizeof(config));
    fake.results[0] = result;
    fake.statuses[0] = status;
    fake.results[1] = 0;
    fake.statuses[1] = 101;
    fake.result_count = 2;
    fake.location = location;
    config.uri = strdup(original_uri);
    config.host = strdup(original_host);
    config.path = strdup("/voice/session");
    config.scheme = strdup("wss");
    config.headers = strdup("Authorization: Bearer TEST-ONLY\r\n");
    config.auth = strdup("Basic TEST-ONLY");
    config.cert = "TEST-ONLY-PIN";
    config.cert_common_name = original_name;
    config.port = 9443;
    config.auto_reconnect = reconnect;
    config.network_timeout_ms = 500;
    client.config = &config;
    client.transport = &fake;
    client.task_handle = 1;
    client.wait_timeout_ms = 10;
}
static void unchanged(void)
{
    CHECK(!strcmp(config.uri, original_uri));
    CHECK(!strcmp(config.host, original_host));
    CHECK(!strcmp(config.path, "/voice/session"));
    CHECK(!strcmp(config.scheme, "wss"));
    CHECK(!strcmp(config.auth, "Basic TEST-ONLY"));
    CHECK(!strcmp(config.headers, "Authorization: Bearer TEST-ONLY\r\n"));
    CHECK(!strcmp(config.cert, "TEST-ONLY-PIN"));
    CHECK(config.cert_common_name == original_name);
    CHECK(!config.skip_cert_common_name_check);
    CHECK(config.port == 9443);
    CHECK(fake.uri_updates == 0 && fake.location_reads == 0 && fake.route_changes == 0);
}
static void teardown(void)
{
    CHECK(fake.events[WEBSOCKET_EVENT_FINISH] == 1);
    CHECK(fake.deleted == 1 && fake.lock_depth == 0 && fake.errors == 0);
    CHECK(client.state == WEBSOCKET_STATE_UNKNOW && !client.run);
    CHECK(client.errormsg_buffer == NULL && client.errormsg_size == 0);
    CHECK(fake.closes >= 1);
    if (!client.selected_for_destroying) CHECK(fake.bits & STOPPED_BIT);
    free(config.uri); free(config.host); free(config.path); free(config.scheme);
    free(config.headers); free(config.auth);
    free(client.errormsg_buffer);
}
static void rejected(int result, int status, const char *location)
{
    setup(false, result, status, location);
    esp_websocket_client_task(&client);
    CHECK(fake.connects == 1);
    CHECK(fake.events[WEBSOCKET_EVENT_CONNECTED] == 0);
    CHECK(fake.events[WEBSOCKET_EVENT_ERROR] == 1);
    CHECK(fake.events[WEBSOCKET_EVENT_DISCONNECTED] == 1);
    CHECK(fake.error_status == status && fake.disconnected_status == status);
    CHECK(fake.error_type == WEBSOCKET_ERROR_TYPE_HANDSHAKE);
    CHECK(fake.disconnected_type == WEBSOCKET_ERROR_TYPE_HANDSHAKE);
    CHECK(fake.closes == 2);
    CHECK(strstr(fake.message, "TEST-ONLY") == NULL);
    CHECK(strstr(fake.message, "redirect.invalid") == NULL);
    unchanged();
    teardown();
}
static void *task_thread(void *data) { esp_websocket_client_task(data); return NULL; }
static void lifecycle(void)
{
    setup(true, 307, 307, "wss://redirect.invalid/secret");
    fake.stop_during_wait = true;
    pthread_t task;
    CHECK(pthread_create(&task, NULL, task_thread, &client) == 0);
    pthread_mutex_lock(&bits_lock);
    while (!fake.wait_reached) pthread_cond_wait(&bits_changed, &bits_lock);
    pthread_mutex_unlock(&bits_lock);
    CHECK(esp_websocket_client_stop(&client) == ESP_OK);
    CHECK(pthread_join(task, NULL) == 0);
    CHECK(fake.connects == 1 && fake.wait_calls == 1);
    CHECK(fake.events[WEBSOCKET_EVENT_CONNECTED] == 0);
    CHECK(fake.bits & REQUESTED_STOP_BIT);
    CHECK(esp_websocket_client_stop(&client) == ESP_FAIL);
    unchanged(); teardown();

    setup(false, 308, 308, "wss://redirect.invalid/secret");
    client.selected_for_destroying = true;
    esp_websocket_client_task(&client);
    CHECK(fake.destroyed == 1 && !(fake.bits & STOPPED_BIT));
    unchanged(); teardown();
    CHECK(esp_websocket_client_stop(NULL) == ESP_ERR_INVALID_ARG);
    client.task_handle = 2;
    CHECK(stop_wait_task(&client) == ESP_FAIL);
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "redirect")) {
        const char *locations[] = {"wss://redirect.invalid/secret", "ws://redirect.invalid/secret",
                                   "wss://192.0.2.99:9444/voice", "/different-path", ":bad-url",
                                   "wss://192.0.2.10:9443/same-origin-other-path"};
        for (int code = 300; code < 400; ++code)
            for (unsigned i = 0; i < sizeof(locations) / sizeof(locations[0]); ++i)
                rejected(code, code, locations[i]);
    } else if (!strcmp(argv[1], "status")) {
        for (int code = 300; code < 400; ++code) rejected(0, code, NULL);
    } else if (!strcmp(argv[1], "missing_location")) {
        rejected(302, 302, NULL);
    } else if (!strcmp(argv[1], "reconnect")) {
        for (int code = 300; code < 400; ++code) {
            setup(true, code, code, "wss://redirect.invalid/secret");
            esp_websocket_client_task(&client);
            CHECK(fake.connects == 2 && fake.wait_calls == 1);
            CHECK(fake.events[WEBSOCKET_EVENT_CONNECTED] == 1);
            CHECK(fake.events[WEBSOCKET_EVENT_DISCONNECTED] == 1);
            CHECK(fake.error_status == code && fake.error_type == WEBSOCKET_ERROR_TYPE_HANDSHAKE);
            unchanged(); teardown();
        }
    } else if (!strcmp(argv[1], "lifecycle")) {
        lifecycle();
    } else if (!strcmp(argv[1], "baseline")) {
        setup(false, 0, 101, NULL);
        esp_websocket_client_task(&client);
        CHECK(fake.connects == 1 && fake.events[WEBSOCKET_EVENT_CONNECTED] == 1);
        CHECK(fake.events[WEBSOCKET_EVENT_DISCONNECTED] == 0 && fake.events[WEBSOCKET_EVENT_ERROR] == 0);
        unchanged(); teardown();
        for (int tls = 0; tls < 2; ++tls) {
            setup(false, -1, tls ? 0 : 302, NULL);
            fake.with_tls_error = tls;
            esp_websocket_client_task(&client);
            CHECK(fake.connects == 1 && fake.events[WEBSOCKET_EVENT_CONNECTED] == 0);
            CHECK(fake.events[WEBSOCKET_EVENT_DISCONNECTED] == 1);
            CHECK(fake.disconnected_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT);
            CHECK(fake.disconnected_status == (tls ? 0 : 302));
            unchanged(); teardown();
        }
    } else return 2;
    fprintf(stderr, "%s: %d failed assertions\n", argv[1], failures);
    return failures ? 1 : 0;
}
