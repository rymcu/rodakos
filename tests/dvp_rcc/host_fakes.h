#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
typedef int esp_err_t;
typedef int gpio_num_t;

#define CAP_DVP_PERIPH_NUM 1
#define TAG "dvp_rcc_test"
#define SOC_PERIPH_CLK_CTRL_SHARED 1
#define __DECLARE_RCC_RC_ATOMIC_ENV __rodak_rcc_env
#define __DECLARE_RCC_ATOMIC_ENV __rodak_atomic_env

typedef enum {
    PERIPH_UART1_MODULE,
    PERIPH_UART2_MODULE,
    PERIPH_LCD_CAM_MODULE,
    PERIPH_MODULE_MAX
} shared_periph_module_t;

typedef struct {
    struct {
        shared_periph_module_t module;
    } buses[1];
} cam_signal_conn_t;

extern const cam_signal_conn_t cam_periph_signals;

uint8_t periph_rcc_acquire_enter(shared_periph_module_t periph);
void periph_rcc_acquire_exit(shared_periph_module_t periph, uint8_t ref_count);
uint8_t periph_rcc_release_enter(shared_periph_module_t periph);
void periph_rcc_release_exit(shared_periph_module_t periph, uint8_t ref_count);
void periph_rcc_enter(void);
void periph_rcc_exit(void);

/* These are copied from the reviewed IDF 6.0.2 periph_ctrl.h macros. */
#define PERIPH_RCC_ACQUIRE_ATOMIC(rc_periph, rc_name) \
    for (uint8_t rc_name, _rc_cnt = 1, __DECLARE_RCC_RC_ATOMIC_ENV __attribute__((unused)); \
         _rc_cnt ? (rc_name = periph_rcc_acquire_enter(rc_periph), 1) : 0; \
         periph_rcc_acquire_exit(rc_periph, rc_name), _rc_cnt--)
#define PERIPH_RCC_RELEASE_ATOMIC(rc_periph, rc_name) \
    for (uint8_t rc_name, _rc_cnt = 1, __DECLARE_RCC_RC_ATOMIC_ENV __attribute__((unused)); \
         _rc_cnt ? (rc_name = periph_rcc_release_enter(rc_periph), 1) : 0; \
         periph_rcc_release_exit(rc_periph, rc_name), _rc_cnt--)
#define PERIPH_RCC_ATOMIC() \
    for (int _rc_cnt = 1, __DECLARE_RCC_ATOMIC_ENV __attribute__((unused)); \
         _rc_cnt ? (periph_rcc_enter(), 1) : 0; \
         periph_rcc_exit(), _rc_cnt--)
#define DVP_CAM_CLK_ATOMIC() PERIPH_RCC_ATOMIC()

/* The production function's checked IDF macros and HAL calls. */
#define ESP_RETURN_ON_FALSE(value, error, ...) do { if (!(value)) return (error); } while (0)
void cam_ll_enable_clk(int ctlr_id, bool enable);
void cam_ll_reset_register(int ctlr_id);
void cam_ll_enable_bus_clock(int ctlr_id, bool enable);
