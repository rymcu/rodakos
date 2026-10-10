/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_gt911.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_board_device.h"
#include "esp_board_periph.h"
#include "dev_gpio_expander.h"
#include "dev_camera.h"
#include "esp_io_expander_pca9557.h"

static const char *TAG = "RYMCU_BIGSMART_SETUP";

#define LCD_CS_GPIO  (IO_EXPANDER_PIN_NUM_0)  // PCA9557_GPIO_NUM_0
#define PA_EN_GPIO   (IO_EXPANDER_PIN_NUM_1)  // PCA9557_GPIO_NUM_1
#define DVP_EN_GPIO  (IO_EXPANDER_PIN_NUM_2)  // PCA9557_GPIO_NUM_2
#define NFC_RST_GPIO (IO_EXPANDER_PIN_NUM_3)  // PCA9557_GPIO_NUM_3

esp_err_t io_expander_factory_entry_t(i2c_master_bus_handle_t i2c_handle, const uint16_t dev_addr, esp_io_expander_handle_t *handle_ret)
{
    esp_err_t ret = esp_io_expander_new_i2c_pca9557(i2c_handle, dev_addr, handle_ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create IO expander handle\n");
        return ret;
    }
    return ESP_OK;
}

esp_err_t camera_dvp_board_prepare_entry_t(void)
{
    esp_io_expander_handle_t *io_expander = NULL;
    esp_err_t ret = esp_board_device_get_handle("gpio_expander", (void **)&io_expander);
    if (ret != ESP_OK || io_expander == NULL) {
        ESP_LOGE(TAG, "Failed to get DVP power-control handle: %s", esp_err_to_name(ret));
        return ret != ESP_OK ? ret : ESP_ERR_NOT_FOUND;
    }

    uint32_t initial_level = 0;
    uint32_t disabled_level = 0;
    uint32_t enabled_level = 0;
    ESP_RETURN_ON_ERROR(
        esp_io_expander_get_level(*io_expander, DVP_EN_GPIO, &initial_level),
        TAG, "Read initial DVP_EN level failed");
    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_level(*io_expander, DVP_EN_GPIO, 1),
        TAG, "Disable DVP camera power failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ret = esp_io_expander_get_level(*io_expander, DVP_EN_GPIO, &disabled_level);
    if (ret != ESP_OK || (disabled_level & DVP_EN_GPIO) == 0) {
        (void)esp_io_expander_set_level(*io_expander, DVP_EN_GPIO, 0);
        ESP_LOGE(TAG, "DVP camera did not enter disabled state: %s", esp_err_to_name(ret));
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
    }

    ret = esp_io_expander_set_level(*io_expander, DVP_EN_GPIO, 0);
    if (ret != ESP_OK) {
        (void)esp_io_expander_set_level(*io_expander, DVP_EN_GPIO, 0);
        ESP_LOGE(TAG, "Enable DVP camera power failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(
        esp_io_expander_get_level(*io_expander, DVP_EN_GPIO, &enabled_level),
        TAG, "Read enabled DVP_EN level failed");
    if ((enabled_level & DVP_EN_GPIO) != 0) {
        ESP_LOGE(TAG, "DVP camera did not enter enabled state");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "DVP camera power cycle complete: initial=%d disabled=1 enabled=0",
             (initial_level & DVP_EN_GPIO) != 0 ? 1 : 0);
    return ESP_OK;
}

esp_err_t lcd_panel_factory_entry_t(
    esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
    esp_lcd_panel_handle_t *ret_panel
)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(esp_lcd_panel_dev_config_t));

    esp_err_t ret = esp_lcd_new_panel_st7789(io, &panel_dev_cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New ST7789 panel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Resetting ST7789 panel before enabling LCD power");
    ret = esp_lcd_panel_reset(*ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reset ST7789 panel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_io_expander_handle_t *io_expander = NULL;
    ret = esp_board_device_get_handle("gpio_expander", (void **)&io_expander);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get io_expander device handle: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(*io_expander, LCD_CS_GPIO, 1), TAG, "Set IO expander pin level failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(*io_expander, LCD_CS_GPIO, 0), TAG, "Set IO expander pin level failed");
    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

esp_err_t lcd_touch_factory_entry_t(
    esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *touch_dev_config,
    esp_lcd_touch_handle_t *ret_touch
)
{
    esp_lcd_touch_config_t touch_cfg = {0};
    memcpy(&touch_cfg, touch_dev_config, sizeof(esp_lcd_touch_config_t));
    if (touch_cfg.int_gpio_num != GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Touch interrupt supported");
        touch_cfg.interrupt_callback = NULL;
    }

    esp_err_t ret = esp_lcd_touch_new_i2c_gt911(io, &touch_cfg, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GT911 touch driver: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}
