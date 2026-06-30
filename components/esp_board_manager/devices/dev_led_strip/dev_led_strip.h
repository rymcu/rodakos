/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "led_strip.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_BOARD_DEVICE_LED_STRIP_SUB_TYPE_RMT  "rmt"  /*!< LED strip over RMT */
#define ESP_BOARD_DEVICE_LED_STRIP_SUB_TYPE_SPI  "spi"  /*!< LED strip over SPI */
#define DEV_LED_STRIP_MAX_LOGICAL_LIGHTS         32     /*!< Maximum named logical ranges */

/**
 * @brief  LED strip RMT sub configuration
 */
typedef struct {
    led_strip_rmt_config_t  rmt_config;  /*!< Native led_strip RMT configuration */
} dev_led_strip_rmt_sub_config_t;

/**
 * @brief  LED strip SPI sub configuration
 */
typedef struct {
    led_strip_spi_config_t  spi_config;  /*!< Native led_strip SPI configuration */
} dev_led_strip_spi_sub_config_t;

/**
 * @brief  LED strip device handle
 */
typedef struct {
    led_strip_handle_t  strip_handle;  /*!< Native led_strip handle */
} dev_led_strip_handles_t;

/**
 * @brief  Logical light range exposed by one LED strip device
 */
typedef struct {
    const char *id;          /*!< Stable logical light id, optional */
    const char *title;       /*!< User-facing light title, optional */
    uint32_t    first_led;   /*!< First physical LED index in this logical light */
    uint32_t    led_count;   /*!< Number of physical LEDs controlled together */
} dev_led_strip_logical_light_t;

/**
 * @brief  LED strip device configuration
 */
typedef struct {
    const char                         *name;                 /*!< Device name */
    const char                         *chip;                 /*!< LED strip chip/model name */
    const char                         *sub_type;             /*!< Sub type: rmt or spi */
    led_strip_config_t                  strip_config;         /*!< Native led_strip common configuration */
    dev_led_strip_logical_light_t       logical_lights[DEV_LED_STRIP_MAX_LOGICAL_LIGHTS]; /*!< Optional ranges */
    uint32_t                            logical_light_count;  /*!< Number of logical light ranges */
    union {
        dev_led_strip_rmt_sub_config_t  rmt;
        dev_led_strip_spi_sub_config_t  spi;
    } sub_cfg;
} dev_led_strip_config_t;

/**
 * @brief  Initialize LED strip device
 *
 * @param[in]   cfg            Pointer to LED strip configuration
 * @param[in]   cfg_size       Size of configuration structure
 * @param[out]  device_handle  Pointer to receive dev_led_strip_handles_t
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_led_strip_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize LED strip device
 *
 * @param[in]  device_handle  Pointer to dev_led_strip_handles_t
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_led_strip_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
