#pragma once
#include <stdint.h>
#include <linux/videodev2.h>
#define VIDIOC_S_DQBUF_TIMEOUT 0xdead0001UL
#define V4L2_CTRL_CLASS_ESP_CAM_IOCTL 0x00a70000U
#define ESP_CAM_SENSOR_IOC_S_REG 0x00a70007U
#define ESP_CAM_SENSOR_IOC_G_REG 0x00a70008U
typedef struct {
    uint32_t regaddr;
    uint32_t value;
} esp_cam_sensor_reg_val_t;
