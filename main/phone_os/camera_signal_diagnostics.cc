#include "phone_os/camera_signal_diagnostics.h"

#include <array>
#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#ifdef RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS
#include "esp_board_device.h"
#include "esp_io_expander.h"
#include "driver/pulse_cnt.h"
#endif

namespace rodakos {
namespace {

constexpr const char* TAG = "CameraService";
constexpr std::array<int, 3> kSignalGpios = {5, 7, 44};
constexpr std::array<const char*, 3> kSignalNames = {"xclk", "pclk", "vsync"};
constexpr uint32_t kDvpEnablePinMask = 1u << 2;

}  // namespace

struct CameraSignalDiagnostics::Counter {
#ifdef RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS
    pcnt_unit_handle_t unit = nullptr;
    pcnt_channel_handle_t channel = nullptr;
    bool enabled = false;
#endif
    int gpio = -1;
    const char* name = "unknown";
    bool active = false;
};

bool CameraSignalDiagnostics::StartCounter(Counter& counter, int gpio, const char* name) {
    counter.gpio = gpio;
    counter.name = name;
#ifdef RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS
    pcnt_unit_config_t unit_config = {};
    unit_config.low_limit = -32768;
    unit_config.high_limit = 32767;
    unit_config.flags.accum_count = 1;
    if (pcnt_new_unit(&unit_config, &counter.unit) != ESP_OK) {
        ESP_LOGW(TAG, "DVP signal diagnostics could not allocate %s counter", name);
        return false;
    }
    if (pcnt_unit_add_watch_point(counter.unit, unit_config.high_limit) != ESP_OK ||
        pcnt_unit_add_watch_point(counter.unit, unit_config.low_limit) != ESP_OK) {
        StopCounter(counter);
        ESP_LOGW(TAG, "DVP signal diagnostics could not configure %s counter watch points", name);
        return false;
    }

    pcnt_chan_config_t channel_config = {};
    channel_config.edge_gpio_num = gpio;
    channel_config.level_gpio_num = -1;
    if (pcnt_new_channel(counter.unit, &channel_config, &counter.channel) != ESP_OK ||
        pcnt_channel_set_edge_action(counter.channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_HOLD) != ESP_OK ||
        pcnt_channel_set_level_action(counter.channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_KEEP) != ESP_OK ||
        pcnt_unit_enable(counter.unit) != ESP_OK) {
        StopCounter(counter);
        ESP_LOGW(TAG, "DVP signal diagnostics could not start %s counter on GPIO%d", name, gpio);
        return false;
    }
    counter.enabled = true;
    if (pcnt_unit_clear_count(counter.unit) != ESP_OK || pcnt_unit_start(counter.unit) != ESP_OK) {
        StopCounter(counter);
        ESP_LOGW(TAG, "DVP signal diagnostics could not start %s counter on GPIO%d", name, gpio);
        return false;
    }
    counter.active = true;
    return true;
#else
    (void)gpio;
    (void)name;
    return false;
#endif
}

void CameraSignalDiagnostics::StopCounter(Counter& counter) {
#ifdef RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS
    if (counter.unit != nullptr) {
        pcnt_unit_stop(counter.unit);
        if (counter.enabled) pcnt_unit_disable(counter.unit);
    }
    if (counter.channel != nullptr) pcnt_del_channel(counter.channel);
    if (counter.unit != nullptr) pcnt_del_unit(counter.unit);
    counter.unit = nullptr;
    counter.channel = nullptr;
    counter.enabled = false;
#endif
    counter.active = false;
}

CameraSignalDiagnostics::~CameraSignalDiagnostics() { Stop(); }

int CameraSignalDiagnostics::ReadAndClear(Counter& counter) {
#ifdef RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS
    if (!counter.active || counter.unit == nullptr) return 0;
    int value = 0;
    if (pcnt_unit_get_count(counter.unit, &value) != ESP_OK) return 0;
    pcnt_unit_clear_count(counter.unit);
    return value;
#else
    (void)counter;
    return 0;
#endif
}

bool CameraSignalDiagnostics::Start() {
#ifndef RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS
    return true;
#else
    Stop();
    esp_io_expander_handle_t* expander = nullptr;
    uint32_t dvp_enable_level = 0;
    const esp_err_t expander_ret =
        esp_board_device_get_handle("gpio_expander", reinterpret_cast<void**>(&expander));
    const esp_err_t dvp_enable_ret = expander_ret == ESP_OK
                                         ? esp_io_expander_get_level(*expander, kDvpEnablePinMask,
                                                                     &dvp_enable_level)
                                         : expander_ret;
    ESP_LOGW(TAG,
             "RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE camera_signal_diagnostics=1 "
             "dvp_en_level=%d dvp_en_read_ok=%d",
             (dvp_enable_level & kDvpEnablePinMask) != 0 ? 1 : 0,
             dvp_enable_ret == ESP_OK ? 1 : 0);
    counters_ = new Counter[3]{};
    bool started = true;
    for (size_t i = 0; i < kSignalGpios.size(); ++i) {
        started = StartCounter(counters_[i], kSignalGpios[i], kSignalNames[i]) && started;
    }
    last_sample_us_ = esp_timer_get_time();
    ESP_LOGW(TAG, "RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE camera_signal_diagnostics=1 started=%d",
             started ? 1 : 0);
    return started;
#endif
}

CameraSignalSample CameraSignalDiagnostics::Sample(const char* phase) {
    CameraSignalSample sample;
#ifdef RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS
    const int64_t now = esp_timer_get_time();
    sample.interval_us = now - last_sample_us_;
    last_sample_us_ = now;
    if (counters_ != nullptr) {
        sample.xclk_edges = ReadAndClear(counters_[0]);
        sample.pclk_edges = ReadAndClear(counters_[1]);
        sample.vsync_edges = ReadAndClear(counters_[2]);
    }
    ESP_LOGW(TAG,
             "RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE camera_signal_diagnostics=1 phase=%s "
             "interval_us=%" PRId64 " xclk_edges=%d pclk_edges=%d vsync_edges=%d",
             phase == nullptr ? "unknown" : phase, sample.interval_us, sample.xclk_edges,
             sample.pclk_edges, sample.vsync_edges);
#else
    (void)phase;
#endif
    return sample;
}

void CameraSignalDiagnostics::Stop() {
#ifdef RODAKOS_CAMERA_SIGNAL_DIAGNOSTICS
    if (counters_ != nullptr) {
        for (size_t i = 0; i < kSignalGpios.size(); ++i) StopCounter(counters_[i]);
        delete[] counters_;
        counters_ = nullptr;
    }
#endif
    last_sample_us_ = 0;
}

}  // namespace rodakos
