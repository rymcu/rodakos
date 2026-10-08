#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fakes.h"
#include RODAK_DVP_RCC_SOURCE

static uint8_t ref_counts[PERIPH_MODULE_MAX];
static bool bus_clock_enabled;
static unsigned reset_calls;
static unsigned bus_clock_enable_calls;
static unsigned bus_clock_disable_calls;
static unsigned clock_enable_calls;
static unsigned clock_disable_calls;
static unsigned critical_depth;
static const char *scenario;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL %s: %s\n", scenario, message);
    exit(1);
}

static void check(bool value, const char *message)
{
    if (!value) fail(message);
}

void periph_rcc_enter(void) { ++critical_depth; }
void periph_rcc_exit(void) { check(critical_depth != 0, "RCC critical section underflow"); --critical_depth; }
uint8_t periph_rcc_acquire_enter(shared_periph_module_t periph)
{
    periph_rcc_enter();
    return ref_counts[periph];
}
void periph_rcc_acquire_exit(shared_periph_module_t periph, uint8_t ref_count)
{
    ref_counts[periph] = (uint8_t)(ref_count + 1);
    periph_rcc_exit();
}
uint8_t periph_rcc_release_enter(shared_periph_module_t periph)
{
    periph_rcc_enter();
    check(ref_counts[periph] != 0, "RCC release underflow");
    return (uint8_t)(ref_counts[periph] - 1);
}
void periph_rcc_release_exit(shared_periph_module_t periph, uint8_t ref_count)
{
    ref_counts[periph] = ref_count;
    periph_rcc_exit();
}

const cam_signal_conn_t cam_periph_signals = {
    .buses = {{.module = PERIPH_LCD_CAM_MODULE}}
};

void cam_ll_enable_clk(int ctlr_id, bool enable)
{
    (void)ctlr_id;
    if (enable) {
        ++clock_enable_calls;
    } else {
        ++clock_disable_calls;
    }
}
void cam_ll_reset_register(int ctlr_id)
{
    (void)ctlr_id;
    ++reset_calls;
}
void cam_ll_enable_bus_clock(int ctlr_id, bool enable)
{
    (void)ctlr_id;
    bus_clock_enabled = enable;
    if (enable) ++bus_clock_enable_calls;
    else ++bus_clock_disable_calls;
}

static void reset_state(void)
{
    memset(ref_counts, 0, sizeof(ref_counts));
    bus_clock_enabled = false;
    reset_calls = bus_clock_enable_calls = bus_clock_disable_calls = 0;
    clock_enable_calls = clock_disable_calls = 0;
    critical_depth = 0;
}

static void owner_acquire(void)
{
    PERIPH_RCC_ACQUIRE_ATOMIC(PERIPH_LCD_CAM_MODULE, ref_count) {
        if (ref_count == 0) {
            cam_ll_enable_bus_clock(0, true);
            cam_ll_reset_register(0);
        }
    }
}

static void owner_release(void)
{
    PERIPH_RCC_RELEASE_ATOMIC(PERIPH_LCD_CAM_MODULE, ref_count) {
        if (ref_count == 0) cam_ll_enable_bus_clock(0, false);
    }
}

static void dvp_acquire(void)
{
    owner_acquire();
}

static void scenario_single(void)
{
    reset_state();
    dvp_acquire();
    check(esp_cam_ctlr_dvp_deinit(0) == ESP_OK, "single deinit succeeds");
    check(ref_counts[PERIPH_LCD_CAM_MODULE] == 0, "single deinit releases RCC reference");
    check(!bus_clock_enabled && bus_clock_disable_calls == 1, "single deinit gates the bus clock");
    check(clock_disable_calls == 1 && critical_depth == 0, "single deinit closes clock critical section");
}

static void scenario_repeat(void)
{
    reset_state();
    for (unsigned cycle = 0; cycle < 32; ++cycle) {
        dvp_acquire();
        check(esp_cam_ctlr_dvp_deinit(0) == ESP_OK, "repeated deinit succeeds");
        check(ref_counts[PERIPH_LCD_CAM_MODULE] == 0, "repeated cycle has no RCC leak");
        check(!bus_clock_enabled, "repeated cycle gates the bus clock");
    }
    check(bus_clock_disable_calls == 32, "every cycle releases the final owner");
}

static void scenario_shared_owner(void)
{
    reset_state();
    owner_acquire();
    dvp_acquire();
    check(ref_counts[PERIPH_LCD_CAM_MODULE] == 2, "two LCD_CAM owners are tracked");
    check(esp_cam_ctlr_dvp_deinit(0) == ESP_OK, "shared-owner deinit succeeds");
    check(ref_counts[PERIPH_LCD_CAM_MODULE] == 1, "DVP release preserves other owner");
    check(bus_clock_enabled && bus_clock_disable_calls == 0, "DVP does not gate shared bus early");
    owner_release();
    check(ref_counts[PERIPH_LCD_CAM_MODULE] == 0 && !bus_clock_enabled,
          "last LCD_CAM owner gates the bus");
}

static void scenario_invalid(void)
{
    reset_state();
    check(esp_cam_ctlr_dvp_deinit(1) == ESP_ERR_INVALID_ARG, "invalid controller id is rejected");
    check(ref_counts[PERIPH_LCD_CAM_MODULE] == 0 && critical_depth == 0, "invalid call has no RCC effects");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    scenario = argv[1];
    if (strcmp(scenario, "single") == 0) scenario_single();
    else if (strcmp(scenario, "repeat") == 0) scenario_repeat();
    else if (strcmp(scenario, "shared_owner") == 0) scenario_shared_owner();
    else if (strcmp(scenario, "invalid") == 0) scenario_invalid();
    else return 2;
    printf("PASS %s\n", scenario);
    return 0;
}
