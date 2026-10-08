#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP_VIDEO_ENABLE_SCCB_DEVICE 1
#define CONFIG_ESP_VIDEO_ENABLE_SCCB_DEVICE 1
#define CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE 1
#define CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_ENC_VIDEO_DEVICE 1
#define ESP_VIDEO_INIT_FLAGS_DVP 1u
#define ESP_VIDEO_INIT_FLAGS_JPEG_ENC 2u
#define ESP_VIDEO_INIT_FLAGS_ALL 3u
#define ESP_CAM_SENSOR_DVP 1
#define CAM_CLK_SRC_DEFAULT 0
#define I2C_NUM_MAX 1
#define DVP_NAME "DVP"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
typedef int esp_err_t;
typedef int gpio_num_t;
typedef int _lock_t;
typedef void *i2c_master_bus_handle_t;
typedef void *jpeg_encoder_handle_t;
typedef struct host_sccb *esp_sccb_io_handle_t;
typedef struct esp_cam_sensor_device {
    int reset_pin;
    int pwdn_pin;
    esp_sccb_io_handle_t sccb_handle;
} esp_cam_sensor_device_t;
typedef struct { esp_cam_sensor_device_t *sensor; } esp_video_cam_t;
typedef struct { int port; } esp_cam_sensor_detect_fn_t;
typedef struct { int unused; } esp_video_init_sccb_config_t;
typedef struct { int xclk_io; } host_dvp_pin_t;
typedef struct {
    esp_video_init_sccb_config_t sccb_config;
    int reset_pin;
    int pwdn_pin;
    host_dvp_pin_t dvp_pin;
    int xclk_freq;
} esp_video_init_dvp_config_t;
typedef struct { jpeg_encoder_handle_t enc_handle; } host_jpeg_config_t;
typedef struct {
    const esp_video_init_dvp_config_t *dvp;
    const host_jpeg_config_t *jpeg_enc;
} esp_video_init_config_t;

#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_RETURN_ON_ERROR(call, ...) do { esp_err_t e = (call); if (e != ESP_OK) return e; } while (0)
#define ESP_RETURN_ON_FALSE(value, error, ...) do { if (!(value)) return error; } while (0)
#define ESP_GOTO_ON_ERROR(call, label, ...) do { ret = (call); if (ret != ESP_OK) goto label; } while (0)

esp_err_t esp_cam_sensor_del_dev(esp_cam_sensor_device_t *sensor);
esp_err_t esp_video_device_common_get_video_cam(const char *name, esp_video_cam_t *cam);
esp_err_t esp_video_destroy_dvp_video_device(void);
esp_err_t esp_sccb_del_i2c_io(esp_sccb_io_handle_t sccb);
esp_err_t esp_cam_ctlr_dvp_deinit(int id);
esp_err_t esp_cam_ctlr_dvp_init_ext(int id, int source, const host_dvp_pin_t *pins);
esp_err_t esp_cam_ctlr_dvp_output_clock(int id, int source, int hz);
esp_err_t esp_video_create_dvp_video_device(esp_cam_sensor_device_t *sensor);
esp_err_t esp_video_create_jpeg_enc_video_device(jpeg_encoder_handle_t handle);
esp_err_t esp_video_destroy_jpeg_enc_video_device(void);
void esp_cam_sensor_detect_get_array(esp_cam_sensor_detect_fn_t **start,
                                    esp_cam_sensor_detect_fn_t **end);
void _lock_acquire_recursive(_lock_t *lock);
void _lock_release_recursive(_lock_t *lock);
esp_err_t gpio_reset_pin(int pin);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus);

esp_err_t esp_video_init_with_flags(const esp_video_init_config_t *, uint32_t);
esp_err_t esp_video_deinit_with_flags(uint32_t);
esp_err_t esp_video_init(const esp_video_init_config_t *);
esp_err_t esp_video_deinit(void);

/* Initialization below this production DTO is the injected hardware boundary. */
struct video_device_init_config;
esp_err_t host_initialize(const struct video_device_init_config *config);
#define esp_video_init_sensor_and_video_device host_initialize
