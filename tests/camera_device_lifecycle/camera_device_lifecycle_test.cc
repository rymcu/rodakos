#include "rodakos_adapters/camera_device.h"
#include <cstdio>
#include <cstring>

extern "C" void fixture_reset(void);
extern "C" void fixture_set_subtype_result(int);
extern "C" void fixture_set_get_failure(bool);
extern "C" void fixture_set_invalid_path(bool);
extern "C" void fixture_set_missing_callback(bool);
extern "C" int fixture_subtype_deinit_calls(void);
extern "C" bool fixture_has_board_handle(void);

static int failures;
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

static int release_failure_retry() {
    fixture_reset();
    rodakos::CameraDevice camera;
    check(camera.Acquire() == ESP_OK, "acquire succeeds");
    fixture_set_subtype_result(ESP_FAIL);
    check(camera.Release() != ESP_OK, "release failure propagates");
    check(camera.acquired(), "failed release retains ownership");
    fixture_set_subtype_result(ESP_OK);
    check(camera.Acquire() == ESP_OK, "acquire retries release then initializes");
    check(fixture_subtype_deinit_calls() == 2, "retry calls subtype deinit");
    check(camera.Release() == ESP_OK, "final release succeeds");
    check(!fixture_has_board_handle(), "board handle cleared after final release");
    return failures ? 1 : 0;
}

static int invalid_handle_retry() {
    fixture_reset();
    rodakos::CameraDevice camera;
    fixture_set_get_failure(true);
    fixture_set_subtype_result(ESP_FAIL);
    check(camera.Acquire() != ESP_OK, "invalid handle acquire fails");
    check(camera.acquired(), "failed cleanup retains ownership");
    fixture_set_get_failure(false);
    fixture_set_subtype_result(ESP_OK);
    check(camera.Acquire() == ESP_OK, "retry releases invalid init then initializes");
    check(camera.Release() == ESP_OK, "release after retry succeeds");
    fixture_reset();
    fixture_set_invalid_path(true);
    fixture_set_subtype_result(ESP_FAIL);
    check(camera.Acquire() != ESP_OK, "invalid device path acquire fails");
    check(camera.acquired(), "invalid path cleanup retains ownership");
    fixture_set_invalid_path(false);
    fixture_set_subtype_result(ESP_OK);
    check(camera.Acquire() == ESP_OK, "invalid path retry releases then initializes");
    check(camera.Release() == ESP_OK, "invalid path final release succeeds");
    return failures ? 1 : 0;
}

static int missing_callback() {
    fixture_reset();
    rodakos::CameraDevice camera;
    check(camera.Acquire() == ESP_OK, "acquire succeeds");
    fixture_set_missing_callback(true);
    check(camera.Release() != ESP_OK, "missing subtype callback propagates");
    check(camera.acquired(), "missing callback retains ownership");
    fixture_set_missing_callback(false);
    fixture_set_subtype_result(ESP_OK);
    check(camera.Acquire() == ESP_OK, "retry after callback restoration succeeds");
    check(camera.Release() == ESP_OK, "release succeeds");
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    if (std::strcmp(argv[1], "release_failure_retry") == 0) return release_failure_retry();
    if (std::strcmp(argv[1], "invalid_handle_cleanup_retry") == 0) return invalid_handle_retry();
    if (std::strcmp(argv[1], "missing_subtype_callback") == 0) return missing_callback();
    return 1;
}
