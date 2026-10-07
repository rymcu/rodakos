#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "camera-teardown-diagnostics.h"
#include "production_mode.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int esp_err_t;
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef int gpio_num_t;
typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef void* gdma_channel_handle_t;
typedef void* esp_cam_ctlr_handle_t;
typedef struct {
    int unused;
} cam_hal_context_t;
typedef struct {
    void* unused;
} portMUX_TYPE;
typedef struct {
    uint32_t words[3];
} dma_descriptor_t;
typedef struct {
    uint8_t* buffer;
    size_t buflen;
    size_t received_size;
} esp_cam_ctlr_trans_t;
typedef struct {
    bool (*on_get_new_trans)(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t*, void*);
    bool (*on_trans_finished)(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t*, void*);
} esp_cam_ctlr_evt_cbs_t;
typedef struct {
    esp_err_t (*del)(esp_cam_ctlr_handle_t);
    esp_err_t (*enable)(esp_cam_ctlr_handle_t);
    esp_err_t (*start)(esp_cam_ctlr_handle_t);
    esp_err_t (*stop)(esp_cam_ctlr_handle_t);
    esp_err_t (*disable)(esp_cam_ctlr_handle_t);
    esp_err_t (*register_event_callbacks)(esp_cam_ctlr_handle_t, const esp_cam_ctlr_evt_cbs_t*,
                                          void*);
    void* get_internal_buffer;
    void* get_buffer_len;
    void* alloc_buffer;
    void* format_conversion;
} esp_cam_ctlr_t;
typedef struct {
    int xclk_io;
    int vsync_io;
} fake_pin_t;
typedef struct {
    int ctlr_id;
    bool pin_dont_init;
    bool external_xtal;
    fake_pin_t* pin;
    int xclk_freq;
    bool pic_format_jpeg;
} esp_cam_ctlr_dvp_config_t;
typedef struct {
    int port;
    int cam_data_width;
    bool bit_swap_en;
    bool byte_swap_en;
} cam_hal_config_t;
typedef struct {
    void* on_recv_eof;
} gdma_rx_event_callbacks_t;
#include "production_types.h"
#define IRAM_ATTR
#define ESP_OK 0
#define ESP_ERR_NO_MEM 257
#define ESP_ERR_INVALID_ARG 258
#define ESP_ERR_INVALID_STATE 259
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t) - 1)
#define pdMS_TO_TICKS(n) (n)
#define MALLOC_CAP_8BIT 1u
#define MALLOC_CAP_INTERNAL 2u
#define MALLOC_CAP_DMA 4u
#define MALLOC_CAP_SPIRAM 8u
#define portMUX_INITIALIZER_UNLOCKED {NULL}
#define DVP_CAM_TASK_STACK_SIZE 3072
#define DVP_CAM_TASK_PRIORITY 23
#define DVP_CAM_TASK_NAME "dvp_task"
#define DVP_CAM_EVENT_QUEUE_SIZE 3
#define DVP_CAM_BUFFER_COUNT 2
#define DVP_CAM_JPEG_DMA_DESC_SIZE 512
#define DVP_CAM_DMA_DESC_BUFFER_SIZE 4092
#define DVP_CAM_DMA_BUFFER_SIZE 8192
#define DVP_CAM_BUS_IO_NUM 8
#define LCD_CAM_PERIPH_NUM 1
#define GPIO_NUM_NC -1
#define GPIO_INTR_NEGEDGE 1
#define ESP_INTR_FLAG_LOWMED 1
#define ESP_INTR_FLAG_IRAM 2
#define DVP_CAM_UP_ALIGN(v, a) (((v) + (a) - 1) & ~((a) - 1))
#define DVP_CAM_CUR_BUF(d) (&((d)->dma_buffer[(d)->dma_desc_index * (d)->dma_buffer_hsize]))
#define ESP_RETURN_ON_FALSE(c, e, ...) \
    do {                               \
        if (!(c)) return (e);          \
    } while (0)
#define ESP_RETURN_ON_ERROR(c, ...) \
    do {                            \
        int r = (c);                \
        if (r) return r;            \
    } while (0)
#define ESP_GOTO_ON_FALSE(c, e, l, ...) \
    do {                                \
        if (!(c)) {                     \
            ret = (e);                  \
            goto l;                     \
        }                               \
    } while (0)
#define ESP_GOTO_ON_ERROR(c, l, ...) \
    do {                             \
        ret = (c);                   \
        if (ret) goto l;             \
    } while (0)
#define ESP_LOGE(...) worker_log(__VA_ARGS__)
#define ESP_LOGW(...) worker_log(__VA_ARGS__)
#define ESP_LOGI(...) worker_log(__VA_ARGS__)
#define ESP_EARLY_LOGE(...) worker_log(__VA_ARGS__)
#define DVP_CAM_ERROR(...) worker_log("dvp_ext", __VA_ARGS__)
#define portENTER_CRITICAL(p) worker_lock(p)
#define portEXIT_CRITICAL(p) worker_unlock(p)
void worker_lock(portMUX_TYPE*);
void worker_unlock(portMUX_TYPE*);
void worker_log(const char*, const char*, ...);
uint32_t xPortGetCoreID(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskDelete(TaskHandle_t);
void vTaskDeleteWithCaps(TaskHandle_t);
void vTaskSuspend(TaskHandle_t);
void vTaskDelay(TickType_t);
BaseType_t xTaskCreate(void (*)(void*), const char*, size_t, void*, unsigned, TaskHandle_t*);
BaseType_t xTaskCreateWithCaps(void (*)(void*), const char*, size_t, void*, unsigned, TaskHandle_t*,
                               unsigned);
QueueHandle_t xQueueCreate(unsigned, unsigned);
BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t);
BaseType_t xQueueSendToFront(QueueHandle_t, const void*, TickType_t);
void vQueueDelete(QueueHandle_t);
void* heap_caps_calloc(size_t, size_t, unsigned);
void* heap_caps_aligned_alloc(size_t, size_t, unsigned);
void heap_caps_free(void*);
esp_err_t gdma_disconnect(gdma_channel_handle_t);
esp_err_t gdma_del_channel(gdma_channel_handle_t);
esp_err_t gdma_stop(gdma_channel_handle_t);
esp_err_t gpio_intr_disable(int);
esp_err_t gpio_intr_enable(int);
esp_err_t gpio_isr_handler_remove(int);
esp_err_t gpio_install_isr_service(int);
esp_err_t gpio_set_intr_type(int, int);
esp_err_t gpio_isr_handler_add(int, void*, void*);
esp_err_t gdma_get_channel_id(gdma_channel_handle_t, int*);
esp_err_t gdma_register_rx_event_callbacks(gdma_channel_handle_t, const gdma_rx_event_callbacks_t*,
                                           void*);
void cam_hal_stop_streaming(cam_hal_context_t*);
void cam_hal_deinit(cam_hal_context_t*);
void cam_hal_init_ext(cam_hal_context_t*, const cam_hal_config_t*);
esp_err_t dvp_get_frame_size(const esp_cam_ctlr_dvp_config_t*, size_t*);
esp_err_t dvp_dma_init(gdma_channel_handle_t*);
void dvp_config_dma_desc(dma_descriptor_t*, size_t, uint8_t*, size_t, dma_descriptor_t*);
uint32_t get_next_dma_desc_addr(dvp_cam_ctlr_t*);
uint32_t dvp_get_dma_valid_size(dvp_cam_ctlr_t*, uint32_t);
uint32_t dvp_calculate_jpeg_size(const uint8_t*, size_t);
esp_err_t dvp_start_capturing(dvp_cam_ctlr_t*);
#define dvp_vsync_isr NULL
#define dvp_receive_isr NULL
esp_err_t worker_create(esp_cam_ctlr_handle_t*);
esp_err_t worker_delete(esp_cam_ctlr_handle_t);
#ifdef __cplusplus
}
#endif
