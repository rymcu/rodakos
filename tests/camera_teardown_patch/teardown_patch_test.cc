#include "camera_teardown_fakes.h"

#include <algorithm>
#include <cstdarg>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
struct Mark { uint32_t phase; uint32_t core; int32_t status; };
std::vector<Mark> marks;
std::vector<std::string> calls;
std::string fail_at;
std::string block_at;
bool forward_delete = false;
bool drop_records = false;
bool interface_stop = false;
std::jmp_buf stopped;
int failures = 0;
int log_count = 0;
int descriptor;
int buffer;
int queue;
int task;
int channel;
int sensor;
int dma_alloc_slots[8];
std::vector<size_t> dma_alloc_sizes;
std::vector<void *> dma_alloc_pointers;
std::vector<void *> dma_freed_pointers;
std::vector<int> dma_fail_calls;
unsigned dma_log_configured = 0;
unsigned dma_log_selected = 0;
unsigned dma_log_actual = 0;
unsigned dma_log_half = 0;
unsigned dma_log_desc_half = 0;
int dma_log_count = 0;
int dma_fault_log_count = 0;
dvp_cam_ctlr_t controller{};

void reset_controller() {
    controller = {};
    controller.task_handle = &task;
    controller.vsync_pin = 7;
    controller.dma_chan = &channel;
    controller.dma_desc = &descriptor;
    controller.dma_buffer = &buffer;
    controller.event_queue = &queue;
    controller.dvp_fsm = DVP_CAM_FSM_STARTED;
}

void reset_dma_allocator() {
    controller = {};
    controller.dma_desc_size = 4092;
    dma_alloc_sizes.clear();
    dma_alloc_pointers.clear();
    dma_freed_pointers.clear();
    dma_fail_calls.clear();
    dma_log_configured = 0;
    dma_log_selected = 0;
    dma_log_actual = 0;
    dma_log_half = 0;
    dma_log_desc_half = 0;
    dma_log_count = 0;
    dma_fault_log_count = 0;
}

void check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
int called(const char *name) {
    calls.emplace_back(name);
    if (block_at == name) std::longjmp(stopped, 1);
    return fail_at == name ? -37 : ESP_OK;
}
void expect_calls(std::initializer_list<const char *> expected) {
    std::vector<std::string> wanted;
    for (auto name : expected) wanted.emplace_back(name);
    check(calls == wanted, "original call order / early return differs");
}
void expect_phases(std::initializer_list<uint32_t> expected) {
    std::vector<uint32_t> actual;
    for (const auto &mark : marks) {
        actual.push_back(mark.phase);
        check(mark.core == 1, "each mark samples the core");
    }
    check(actual == std::vector<uint32_t>(expected), "marker order differs");
}
void expect_failure_status(uint32_t phase) {
    bool found = false;
    for (const auto &mark : marks) {
        if (mark.phase == phase) { found = true; check(mark.status == -37, "return code not retained"); }
    }
    check(found, "failed operation has no returned marker");
}
esp_err_t stop_interface(esp_video_device_common_t *) { return called("interface"); }
int run_video() {
    fake_interface_t intf{interface_stop ? stop_interface : nullptr};
    esp_video_device_common_t common{{&sensor}, &intf, &controller};
    esp_video video{&common};
    int result = run_common_stop(&video);
    check((common.cam_ctrl_handle == nullptr) == (result == ESP_OK), "handle clearing timing changed");
    return result;
}
}

extern "C" {
uint32_t rodak_camera_teardown_record(uint32_t phase, uint32_t core, int32_t status) {
    marks.push_back({phase, core, status});
    return drop_records ? 0 : static_cast<uint32_t>(marks.size());
}
uint32_t xPortGetCoreID() { return 1; }
void fake_log() { ++log_count; }
void fake_dma_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    dma_log_configured = va_arg(args, unsigned);
    dma_log_selected = va_arg(args, unsigned);
    dma_log_actual = va_arg(args, unsigned);
    dma_log_half = va_arg(args, unsigned);
    dma_log_desc_half = va_arg(args, unsigned);
    va_end(args);
    ++dma_log_count;
}
void fake_dma_fault_log(const char *, ...) { ++dma_fault_log_count; }
esp_err_t esp_cam_sensor_ioctl(void *handle, int command, int *flags) {
    check(handle == &sensor && command == ESP_CAM_SENSOR_IOC_S_STREAM && *flags == 0, "sensor STREAMOFF arguments changed");
    return called("sensor");
}
esp_err_t esp_cam_ctlr_stop(esp_cam_ctlr_handle_t handle) { check(handle == &controller, "stop handle"); return called("stop"); }
esp_err_t esp_cam_ctlr_disable(esp_cam_ctlr_handle_t handle) { check(handle == &controller, "disable handle"); return called("disable"); }
esp_err_t esp_cam_ctlr_del(esp_cam_ctlr_handle_t handle) {
    check(handle == &controller, "delete handle");
    int result = called("delete");
    return result != ESP_OK || !forward_delete ? result : run_dvp_del(&controller);
}
esp_err_t gdma_disconnect(gdma_channel_handle_t handle) { check(handle == &channel, "DMA disconnect handle"); return called("disconnect"); }
esp_err_t gdma_del_channel(gdma_channel_handle_t handle) { check(handle == &channel, "DMA delete handle"); return called("dma_delete"); }
esp_err_t gdma_stop(gdma_channel_handle_t handle) { check(handle == &channel, "DMA stop handle"); return called("capture"); }
void vTaskDelete(void *handle) { check(handle == &task, "task handle"); (void)called("task"); }
void vTaskDeleteWithCaps(void *handle) { vTaskDelete(handle); }
TaskHandle_t xTaskGetCurrentTaskHandle() { return &sensor; }
int xQueueSendToFront(void *, const void *, unsigned) { controller.worker_quiesced = true; return 1; }
void vTaskDelay(unsigned) {}
esp_err_t gpio_intr_disable(int pin) { check(pin == 7, "GPIO disable pin"); return called("gpio_disable"); }
esp_err_t gpio_isr_handler_remove(int pin) { check(pin == 7, "GPIO remove pin"); return called("gpio_remove"); }
void cam_hal_stop_streaming(cam_hal_context_t *hal) { check(hal == &controller.hal, "capture HAL"); (void)called("hal_stop"); }
void cam_hal_deinit(cam_hal_context_t *hal) { check(hal == &controller.hal, "delete HAL"); (void)called("hal_delete"); }
void heap_caps_free(void *pointer) {
    if (std::find(dma_alloc_pointers.begin(), dma_alloc_pointers.end(), pointer) !=
        dma_alloc_pointers.end()) {
        dma_freed_pointers.push_back(pointer);
    } else if (pointer == &descriptor) (void)called("free_desc");
    else if (pointer == &buffer) (void)called("free_buffer");
    else { check(pointer == &controller, "free controller"); (void)called("free_ctlr"); }
}
void *heap_caps_aligned_alloc(size_t align, size_t size, uint32_t) {
    check(align == 4, "DMA allocation alignment changed");
    const int call = static_cast<int>(dma_alloc_sizes.size()) + 1;
    dma_alloc_sizes.push_back(size);
    if (std::find(dma_fail_calls.begin(), dma_fail_calls.end(), call) != dma_fail_calls.end()) {
        return nullptr;
    }
    void *pointer = &dma_alloc_slots[dma_alloc_pointers.size()];
    dma_alloc_pointers.push_back(pointer);
    return pointer;
}
void vQueueDelete(void *handle) { check(handle == &queue, "queue handle"); (void)called("queue"); }
}

int main(int argc, char **argv) {
    reset_controller();
    check(argc == 2, "one test scenario required");
    if (argc != 2) return 1;
    std::string scenario = argv[1];
    if (scenario == "complete" || scenario == "record_drop") {
        forward_delete = true;
        drop_records = scenario == "record_drop";
        check(run_video() == ESP_OK, "complete stop succeeds");
        expect_calls({"stop", "sensor", "disable", "delete", "task", "gpio_disable", "hal_stop", "capture",
                      "hal_delete", "gpio_remove", "disconnect", "dma_delete", "free_desc", "free_buffer", "queue", "free_ctlr"});
        expect_phases({7, 8, 5, 6, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 12});
        check(marks.size() + 4 == 24 && marks.size() + 4 <= RODAK_CAMERA_TEARDOWN_CAPACITY, "root + overlay mark budget");
        check(log_count == 0, "success adds no log calls");
    } else if (scenario.rfind("common_fail_", 0) == 0) {
        fail_at = scenario.substr(12);
        interface_stop = true;
        check(run_video() == -37, "common preserves first failure");
        if (fail_at == "sensor") { expect_calls({"stop", "sensor"}); expect_phases({7, 8, 5, 6}); expect_failure_status(6); }
        else if (fail_at == "interface") { expect_calls({"stop", "sensor", "interface"}); expect_phases({7, 8, 5, 6}); }
        else if (fail_at == "stop") { expect_calls({"stop"}); expect_phases({7, 8}); expect_failure_status(8); }
        else if (fail_at == "disable") { expect_calls({"stop", "sensor", "interface", "disable"}); expect_phases({7, 8, 5, 6, 9, 10}); expect_failure_status(10); }
        else if (fail_at == "delete") { expect_calls({"stop", "sensor", "interface", "disable", "delete"}); expect_phases({7, 8, 5, 6, 9, 10, 11, 12}); expect_failure_status(12); }
        else check(false, "unknown common failure");
        check(log_count == 1, "common retains existing error log count");
    } else if (scenario.rfind("del_fail_", 0) == 0) {
        fail_at = scenario.substr(9);
        check(run_dvp_del(&controller) == -37, "delete propagates cleanup errors");
        if (fail_at == "disconnect") {
            expect_calls({"task", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove", "disconnect"});
            expect_phases({13, 14, 15, 16, 17, 18, 19, 20, 21, 22});
            expect_failure_status(22);
        } else {
            if (fail_at == "dma_delete") {
                expect_calls({"task", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove", "disconnect", "dma_delete"});
                expect_phases({13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24});
                expect_failure_status(24);
            } else if (fail_at == "gpio_disable") {
                expect_calls({"task", "gpio_disable"});
                expect_phases({13, 14, 15, 16});
                expect_failure_status(16);
            } else if (fail_at == "capture") {
                expect_calls({"task", "gpio_disable", "hal_stop", "capture"});
                expect_phases({13, 14, 15, 16, 17, 18});
                expect_failure_status(18);
            } else if (fail_at == "gpio_remove") {
                expect_calls({"task", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove"});
                expect_phases({13, 14, 15, 16, 17, 18, 19, 20});
                expect_failure_status(20);
            }
        }
        check(log_count == (fail_at == "capture" ? 1 : 0), "failed staged cleanup logs only the failed capture stop");
    } else if (scenario.rfind("del_retry_", 0) == 0) {
        fail_at = scenario.substr(10);
        check(run_dvp_del(&controller) == -37, "first staged delete reports failure");
        fail_at.clear();
        check(run_dvp_del(&controller) == ESP_OK, "second staged delete completes");
        if (scenario == "del_retry_gpio_disable") {
            expect_calls({"task", "gpio_disable", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove", "disconnect", "dma_delete", "free_desc", "free_buffer", "queue", "free_ctlr"});
        } else if (scenario == "del_retry_capture") {
            expect_calls({"task", "gpio_disable", "hal_stop", "capture", "hal_stop", "capture", "hal_delete", "gpio_remove", "disconnect", "dma_delete", "free_desc", "free_buffer", "queue", "free_ctlr"});
        } else if (scenario == "del_retry_gpio_remove") {
            expect_calls({"task", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove", "gpio_remove", "disconnect", "dma_delete", "free_desc", "free_buffer", "queue", "free_ctlr"});
        } else if (scenario == "del_retry_disconnect") {
            expect_calls({"task", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove", "disconnect", "disconnect", "dma_delete", "free_desc", "free_buffer", "queue", "free_ctlr"});
        } else if (scenario == "del_retry_dma_delete") {
            expect_calls({"task", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove", "disconnect", "dma_delete", "dma_delete", "free_desc", "free_buffer", "queue", "free_ctlr"});
        } else {
            check(false, "unknown staged retry");
        }
        expect_phases({13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24});
    } else if (scenario == "common_retry_disconnect") {
        forward_delete = true;
        fail_at = "disconnect";
        check(run_video() == -37, "common stop exposes DVP delete failure");
        fail_at.clear();
        check(run_video() == ESP_OK, "common stop retries the staged delete");
        expect_calls({"stop", "sensor", "disable", "delete", "task", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove", "disconnect",
                      "stop", "sensor", "disable", "delete", "disconnect", "dma_delete", "free_desc", "free_buffer", "queue", "free_ctlr"});
        expect_phases({7, 8, 5, 6, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 12,
                       7, 8, 5, 6, 9, 10, 11, 23, 24, 12});
    } else if (scenario == "del_self_reject") {
        controller.task_handle = &sensor;
        check(run_dvp_del(&controller) == ESP_ERR_INVALID_STATE, "self deletion is rejected");
        check(!controller.teardown_started, "self rejection does not arm teardown");
        check(marks.empty(), "self rejection does not consume diagnostics slots");
    } else if (scenario == "startup_cleanup" || scenario == "startup_cleanup_error" || scenario == "null_dma") {
        if (scenario == "startup_cleanup_error") fail_at = "disconnect";
        check(run_dma_deinit(scenario == "null_dma" ? nullptr : &channel, scenario == "null_dma") ==
              (scenario == "startup_cleanup_error" ? -37 : ESP_OK), "DMA cleanup result");
        check(marks.empty(), "startup/null cleanup consumes no slots");
        if (scenario == "startup_cleanup") expect_calls({"disconnect", "dma_delete"});
        else if (scenario == "startup_cleanup_error") expect_calls({"disconnect"});
        else check(calls.empty(), "null DMA remains a no-op");
    } else if (scenario == "dma_preferred" || scenario == "dma_forced_fallback" ||
               scenario == "dma_jpeg_configured" ||
               scenario == "dma_ring_fallback" ||
               scenario == "dma_desc_fallback" || scenario == "dma_exhausted") {
        reset_dma_allocator();
        if (scenario == "dma_jpeg_configured") controller.dma_desc_size = 512;
        if (scenario == "dma_ring_fallback") dma_fail_calls = {1};
        if (scenario == "dma_desc_fallback") dma_fail_calls = {2};
        if (scenario == "dma_exhausted") dma_fail_calls = {2, 4};
        const esp_err_t result = run_dma_allocate(
            &controller, 4, 320 * 240 * 2, scenario == "dma_jpeg_configured");
        if (scenario == "dma_preferred") {
            check(result == ESP_OK, "preferred DMA allocation succeeds");
            check(dma_alloc_sizes == std::vector<size_t>({6144, 32}), "preferred ring and descriptor sizes");
            check(controller.dma_buffer_size == 6144 && controller.dma_buffer_hsize == 3072,
                  "preferred DMA layout differs");
            check(controller.dma_desc_hcnt == 1, "preferred descriptor count differs");
            check(dma_freed_pointers.empty(), "successful preferred allocation was freed");
            check(dma_fault_log_count == 0, "ordinary preferred allocation emitted fault marker");
        } else if (scenario == "dma_forced_fallback") {
            check(result == ESP_OK, "forced DMA fallback succeeds");
            check(dma_alloc_sizes == std::vector<size_t>({4096, 32}),
                  "forced fallback attempted the preferred ring");
            check(controller.dma_buffer_size == 4096 && controller.dma_buffer_hsize == 2048,
                  "forced fallback layout differs");
            check(controller.dma_desc_hcnt == 1, "forced fallback descriptor count differs");
            check(dma_fault_log_count == 1, "forced fallback marker missing");
        } else if (scenario == "dma_jpeg_configured") {
            check(result == ESP_OK, "JPEG DMA allocation succeeds");
            check(dma_alloc_sizes == std::vector<size_t>({8192, 256}),
                  "JPEG keeps the configured ring and descriptor sizes");
            check(controller.dma_buffer_size == 8192 && controller.dma_buffer_hsize == 4096,
                  "JPEG configured DMA layout differs");
            check(controller.dma_desc_hcnt == 8, "JPEG descriptor count differs");
            check(dma_freed_pointers.empty(), "successful JPEG allocation was freed");
        } else if (scenario == "dma_ring_fallback") {
            check(result == ESP_OK, "ring allocation failure falls back");
            check(dma_alloc_sizes == std::vector<size_t>({6144, 4096, 32}),
                  "ring failure fallback order differs");
            check(controller.dma_buffer_size == 4096 && controller.dma_buffer_hsize == 2048,
                  "ring fallback layout differs");
            check(dma_freed_pointers.empty(), "failed ring allocation created ownership");
        } else if (scenario == "dma_desc_fallback") {
            check(result == ESP_OK, "descriptor allocation failure falls back");
            check(dma_alloc_sizes == std::vector<size_t>({6144, 32, 4096, 32}),
                  "descriptor failure fallback order differs");
            check(dma_freed_pointers.size() == 1 &&
                      dma_freed_pointers.front() == dma_alloc_pointers.front(),
                  "descriptor failure did not free the selected ring");
            check(controller.dma_buffer_size == 4096 && controller.dma_buffer_hsize == 2048,
                  "descriptor fallback layout differs");
        } else {
            check(result == ESP_ERR_NO_MEM, "exhausted DMA candidates report no memory");
            check(dma_alloc_sizes == std::vector<size_t>({6144, 32, 4096, 32}),
                  "exhausted candidate order differs");
            check(dma_freed_pointers.size() == 2, "exhausted descriptor failures leaked rings");
            check(controller.dma_buffer == nullptr && controller.dma_desc == nullptr,
                  "exhausted allocation retained pointers");
            check(controller.dma_buffer_size == 0 && controller.dma_buffer_hsize == 0 &&
                      controller.dma_desc_hcnt == 0,
                  "exhausted allocation retained layout state");
        }
        if (result == ESP_OK) {
            check(dma_log_count == 1 && dma_log_configured == 8192,
                  "successful DMA selection log missing configured size");
            const unsigned expected_selected = scenario == "dma_jpeg_configured"
                                                   ? 8192u
                                                   : (scenario == "dma_preferred" ? 6144u : 4096u);
            check(dma_log_selected == expected_selected,
                  "DMA selection log has wrong candidate");
            check(dma_log_actual == controller.dma_buffer_size &&
                      dma_log_half == controller.dma_buffer_hsize &&
                      dma_log_desc_half == controller.dma_desc_hcnt,
                  "DMA selection log differs from controller layout");
        } else {
            check(dma_log_count == 0, "failed DMA selection emitted a success log");
        }
    } else if (scenario.rfind("blocked_", 0) == 0) {
        block_at = scenario.substr(8);
        forward_delete = true;
        if (setjmp(stopped) == 0) { (void)run_video(); check(false, "fake non-returning phase was not reached"); }
        uint32_t expected = 0;
        if (block_at == "sensor") expected = 5;
        else if (block_at == "stop") expected = 7;
        else if (block_at == "disable") expected = 9;
        else if (block_at == "delete") expected = 11;
        else if (block_at == "task") expected = 13;
        else if (block_at == "gpio_disable") expected = 15;
        else if (block_at == "capture") expected = 17;
        else if (block_at == "gpio_remove") expected = 19;
        else if (block_at == "disconnect") expected = 21;
        else if (block_at == "dma_delete") expected = 23;
        check(expected != 0 && !marks.empty() && marks.back().phase == expected, "non-returning operation leaves entry as final marker");
        check(marks.back().status == 0, "entry has zero status");
    } else check(false, "unknown scenario");
    if (failures == 0) std::printf("PASS %s (%zu marks)\n", argv[1], marks.size());
    return failures == 0 ? 0 : 1;
}
