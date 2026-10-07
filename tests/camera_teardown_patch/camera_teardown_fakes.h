#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "camera-teardown-diagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;
typedef void *gdma_channel_handle_t;
typedef void *esp_cam_ctlr_handle_t;
typedef int cam_hal_context_t;
typedef struct dvp_cam_ctlr {
    void *task_handle;
    int vsync_pin;
    gdma_channel_handle_t dma_chan;
    cam_hal_context_t hal;
    void *dma_desc;
    void *dma_buffer;
    void *event_queue;
} dvp_cam_ctlr_t;
typedef struct esp_video_device_common esp_video_device_common_t;
typedef struct {
    esp_err_t (*stop)(esp_video_device_common_t *);
} fake_interface_t;
struct esp_video_device_common {
    struct { void *sensor; } cam;
    fake_interface_t *intf;
    esp_cam_ctlr_handle_t cam_ctrl_handle;
};
struct esp_video { esp_video_device_common_t *common; };
#define VIDEO_DEVICE_COMMON(video) ((video)->common)
#define ESP_OK 0
#define ESP_CAM_SENSOR_IOC_S_STREAM 123
#define ESP_RETURN_ON_ERROR(call, tag, message) do { \
    esp_err_t fake_return = (call); \
    if (fake_return != ESP_OK) { fake_log(); return fake_return; } \
} while (0)
#define ESP_LOGE(...) fake_log()
#define ESP_EARLY_LOGE(...) fake_log()

void fake_log(void);
uint32_t xPortGetCoreID(void);
esp_err_t esp_cam_sensor_ioctl(void *, int, int *);
esp_err_t esp_cam_ctlr_stop(esp_cam_ctlr_handle_t);
esp_err_t esp_cam_ctlr_disable(esp_cam_ctlr_handle_t);
esp_err_t esp_cam_ctlr_del(esp_cam_ctlr_handle_t);
esp_err_t gdma_disconnect(gdma_channel_handle_t);
esp_err_t gdma_del_channel(gdma_channel_handle_t);
esp_err_t gdma_stop(gdma_channel_handle_t);
void vTaskDelete(void *);
esp_err_t gpio_intr_disable(int);
esp_err_t gpio_isr_handler_remove(int);
void cam_hal_stop_streaming(cam_hal_context_t *);
void cam_hal_deinit(cam_hal_context_t *);
void heap_caps_free(void *);
void vQueueDelete(void *);
esp_err_t run_common_stop(struct esp_video *);
esp_err_t run_dvp_del(dvp_cam_ctlr_t *);
esp_err_t run_dma_deinit(gdma_channel_handle_t, bool);

#ifdef __cplusplus
}
#endif
