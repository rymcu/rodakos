#include "camera_teardown_fakes.h"

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
dvp_cam_ctlr_t controller{&task, 7, &channel, 0, &descriptor, &buffer, &queue};

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
esp_err_t gpio_intr_disable(int pin) { check(pin == 7, "GPIO disable pin"); return called("gpio_disable"); }
esp_err_t gpio_isr_handler_remove(int pin) { check(pin == 7, "GPIO remove pin"); return called("gpio_remove"); }
void cam_hal_stop_streaming(cam_hal_context_t *hal) { check(hal == &controller.hal, "capture HAL"); (void)called("hal_stop"); }
void cam_hal_deinit(cam_hal_context_t *hal) { check(hal == &controller.hal, "delete HAL"); (void)called("hal_delete"); }
void heap_caps_free(void *pointer) {
    if (pointer == &descriptor) (void)called("free_desc");
    else if (pointer == &buffer) (void)called("free_buffer");
    else { check(pointer == &controller, "free controller"); (void)called("free_ctlr"); }
}
void vQueueDelete(void *handle) { check(handle == &queue, "queue handle"); (void)called("queue"); }
}

int main(int argc, char **argv) {
    check(argc == 2, "one test scenario required");
    if (argc != 2) return 1;
    std::string scenario = argv[1];
    if (scenario == "complete" || scenario == "record_drop") {
        forward_delete = true;
        drop_records = scenario == "record_drop";
        check(run_video() == ESP_OK, "complete stop succeeds");
        expect_calls({"sensor", "stop", "disable", "delete", "task", "gpio_disable", "hal_stop", "capture",
                      "hal_delete", "gpio_remove", "disconnect", "dma_delete", "free_desc", "free_buffer", "queue", "free_ctlr"});
        expect_phases({5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 12});
        check(marks.size() + 4 == 24 && marks.size() + 4 <= RODAK_CAMERA_TEARDOWN_CAPACITY, "root + overlay mark budget");
        check(log_count == 0, "success adds no log calls");
    } else if (scenario.rfind("common_fail_", 0) == 0) {
        fail_at = scenario.substr(12);
        interface_stop = true;
        check(run_video() == -37, "common preserves first failure");
        if (fail_at == "sensor") { expect_calls({"sensor"}); expect_phases({5, 6}); expect_failure_status(6); }
        else if (fail_at == "interface") { expect_calls({"sensor", "interface"}); expect_phases({5, 6}); }
        else if (fail_at == "stop") { expect_calls({"sensor", "interface", "stop"}); expect_phases({5, 6, 7, 8}); expect_failure_status(8); }
        else if (fail_at == "disable") { expect_calls({"sensor", "interface", "stop", "disable"}); expect_phases({5, 6, 7, 8, 9, 10}); expect_failure_status(10); }
        else if (fail_at == "delete") { expect_calls({"sensor", "interface", "stop", "disable", "delete"}); expect_phases({5, 6, 7, 8, 9, 10, 11, 12}); expect_failure_status(12); }
        else check(false, "unknown common failure");
        check(log_count == 1, "common retains existing error log count");
    } else if (scenario.rfind("del_fail_", 0) == 0) {
        fail_at = scenario.substr(9);
        check(run_dvp_del(&controller) == ESP_OK, "delete still ignores cleanup errors");
        if (fail_at == "disconnect") {
            expect_calls({"task", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove", "disconnect", "free_desc", "free_buffer", "queue", "free_ctlr"});
            expect_phases({13, 14, 15, 16, 17, 18, 19, 20, 21, 22});
            expect_failure_status(22);
        } else {
            expect_calls({"task", "gpio_disable", "hal_stop", "capture", "hal_delete", "gpio_remove", "disconnect", "dma_delete", "free_desc", "free_buffer", "queue", "free_ctlr"});
            expect_phases({13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24});
            if (fail_at == "gpio_disable") expect_failure_status(16);
            else if (fail_at == "capture") expect_failure_status(18);
            else if (fail_at == "gpio_remove") expect_failure_status(20);
            else if (fail_at == "dma_delete") expect_failure_status(24);
            else check(false, "unknown delete failure");
        }
        check(log_count == ((fail_at == "disconnect" || fail_at == "dma_delete") ? 2 : (fail_at == "capture" ? 1 : 0)), "existing cleanup log behavior retained");
    } else if (scenario == "startup_cleanup" || scenario == "startup_cleanup_error" || scenario == "null_dma") {
        if (scenario == "startup_cleanup_error") fail_at = "disconnect";
        check(run_dma_deinit(scenario == "null_dma" ? nullptr : &channel, scenario == "null_dma") ==
              (scenario == "startup_cleanup_error" ? -37 : ESP_OK), "DMA cleanup result");
        check(marks.empty(), "startup/null cleanup consumes no slots");
        if (scenario == "startup_cleanup") expect_calls({"disconnect", "dma_delete"});
        else if (scenario == "startup_cleanup_error") expect_calls({"disconnect"});
        else check(calls.empty(), "null DMA remains a no-op");
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
