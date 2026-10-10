"""DVP 相机失败清理契约的宿主回归检查。

该测试不需要 ESP-IDF 或硬件，直接检查生产 deinit 的控制流：底层
esp_video_deinit 或 I2C 引用释放失败时，句柄必须保留以便 board manager
重试；只有两步都成功后才允许释放句柄。
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/esp_board_manager/devices/dev_camera/dev_camera_sub_dvp.c"
CAMERA_DEVICE_HEADER = ROOT / "main/rodakos_adapters/camera_device.h"
CAMERA_DEVICE_SOURCE = ROOT / "main/rodakos_adapters/camera_device.cc"
CAMERA_CAPTURE_FAKE_HEADER = ROOT / "tests/camera_capture/fakes/rodakos_adapters/camera_device.h"
CAMERA_SERVICE_SOURCE = ROOT / "main/phone_os/camera_service.cc"


def function_body(source: str, name: str) -> str:
    match = re.search(
        rf"\b(?:[A-Za-z_][\w:<>]*\s+)+{re.escape(name)}\s*\([^)]*\)\s*\{{", source
    )
    if match is None:
        raise AssertionError(f"missing {name}")
    start = match.end()
    depth = 1
    index = start
    while depth and index < len(source):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    if depth:
        raise AssertionError(f"unterminated {name}")
    return source[start : index - 1]


class DvpDeinitContractTest(unittest.TestCase):
    def setUp(self) -> None:
        self.body = function_body(SOURCE.read_text(encoding="utf-8"), "dev_camera_sub_dvp_deinit")

    def test_null_handle_is_rejected_before_driver_call(self) -> None:
        guard = re.search(r"if\s*\(\s*device_handle\s*==\s*NULL\s*\)\s*\{(?P<body>.*?)\}", self.body, re.S)
        self.assertIsNotNone(guard)
        self.assertIn("return -1", guard.group("body"))
        self.assertLess(self.body.index("device_handle == NULL"), self.body.index("esp_video_deinit"))

    def test_board_prepare_runs_before_video_initialization(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        init = function_body(source, "dev_camera_sub_dvp_init")
        self.assertIn("camera_dvp_board_prepare_entry_t", init)
        self.assertLess(init.index("camera_dvp_board_prepare_entry_t"), init.index("esp_video_init"))
        self.assertIn("goto cleanup", init[init.index("camera_dvp_board_prepare_entry_t"):])

    def test_video_failure_preserves_handle_and_peripheral_reference(self) -> None:
        failure = re.search(r"if\s*\(\s*ret\s*!=\s*ESP_OK\s*\)\s*\{(?P<body>.*?)\}", self.body, re.S)
        self.assertIsNotNone(failure)
        failure_body = failure.group("body")
        self.assertIn("return -1", failure_body)
        self.assertNotIn("esp_board_periph_unref_handle", failure_body)
        self.assertNotIn("free(", failure_body)
        self.assertLess(self.body.index("esp_video_deinit"), self.body.index("dev_camera_config_t"))

    def test_peripheral_failure_preserves_handle_for_retry(self) -> None:
        unref = self.body.index("esp_board_periph_unref_handle")
        release_check = self.body.find("if (ret != ESP_OK)", unref)
        self.assertGreaterEqual(release_check, unref)
        release_end = self.body.index("free(device_handle)")
        self.assertLess(release_check, release_end)
        failure = self.body[release_check:release_end]
        self.assertIn("return -1", failure)

    def test_handle_is_freed_only_after_i2c_release(self) -> None:
        self.assertLess(self.body.index("esp_board_periph_unref_handle"), self.body.index("free(device_handle)"))
        self.assertIn("return 0", self.body)

    def test_config_lookup_failure_preserves_handle(self) -> None:
        self.assertIn("ret = esp_board_device_get_config_by_handle", self.body)
        self.assertIn("ret != ESP_OK || cfg == NULL", self.body)
        lookup_failure = self.body.index("ret != ESP_OK || cfg == NULL")
        self.assertLess(self.body.index("return -1", lookup_failure), self.body.index("free(device_handle)"))


class CameraDeviceRetryContractTest(unittest.TestCase):
    def setUp(self) -> None:
        self.header = CAMERA_DEVICE_HEADER.read_text(encoding="utf-8")
        source = CAMERA_DEVICE_SOURCE.read_text(encoding="utf-8")
        self.acquire = function_body(source, "CameraDevice::Acquire")
        self.release = function_body(source, "CameraDevice::Release")

    def test_release_reports_error_and_keeps_state_for_retry(self) -> None:
        self.assertIn("esp_err_t Release();", self.header)
        self.assertIn("bool release_retry_required_ = false;", self.header)
        failure = re.search(r"if\s*\(\s*ret\s*!=\s*ESP_OK\s*\)\s*\{(?P<body>.*?)\}", self.release, re.S)
        self.assertIsNotNone(failure)
        failure_body = failure.group("body")
        self.assertIn("release_retry_required_ = true", failure_body)
        self.assertIn("return ret", failure_body)
        self.assertNotIn("acquired_ = false", failure_body)
        self.assertNotIn("dev_path_ = nullptr", failure_body)

    def test_acquire_retries_release_before_initializing(self) -> None:
        self.assertIn("if (acquired_ && !release_retry_required_)", self.acquire)
        retry = self.acquire.index("if (release_retry_required_)")
        release_call = self.acquire.index("Release()", retry)
        init_call = self.acquire.index("esp_board_manager_init_device_by_name")
        self.assertLess(release_call, init_call)
        self.assertIn("if (release_ret != ESP_OK)", self.acquire[retry:init_call])

    def test_release_clears_state_only_after_success(self) -> None:
        reset = self.release.index("acquired_ = false")
        self.assertLess(self.release.index("esp_board_manager_deinit_device_by_name"), reset)
        self.assertLess(reset, self.release.index("return ESP_OK"))


class CameraCaptureFakeContractTest(unittest.TestCase):
    def test_camera_capture_fake_matches_error_returning_release_contract(self) -> None:
        header = CAMERA_CAPTURE_FAKE_HEADER.read_text(encoding="utf-8")
        self.assertRegex(header, r"\besp_err_t\s+Release\s*\(\)\s*\{\s*return\s+ESP_OK;\s*\}")
        self.assertNotRegex(header, r"\bvoid\s+Release\s*\(")


class CameraServiceReleaseLogContractTest(unittest.TestCase):
    def test_close_stream_does_not_report_failed_release_as_complete(self) -> None:
        source = CAMERA_SERVICE_SOURCE.read_text(encoding="utf-8")
        close_stream = function_body(source, "CameraService::CloseStream")
        release = close_stream.index("camera_device_.Release()")
        self.assertIn("const esp_err_t release_ret", close_stream[release - 40 : release + 80])
        self.assertIn("release_ret == ESP_OK", close_stream)
        self.assertIn("release deferred for retry", close_stream)
        self.assertLess(
            close_stream.index("release_ret == ESP_OK"),
            close_stream.index('"CloseStream: device release complete"')
        )

    def test_streamoff_failure_keeps_v4l2_ownership_for_retry(self) -> None:
        source = CAMERA_SERVICE_SOURCE.read_text(encoding="utf-8")
        close_stream = function_body(source, "CameraService::CloseStream")
        failure = close_stream.index("streamoff_result != 0")
        self.assertIn("streamoff_retry_required_", close_stream[failure:])
        self.assertIn("return", close_stream[failure:])
        self.assertLess(
            close_stream.index("streamoff_retry_required_", failure),
            close_stream.index("return", failure)
        )
        failure_body = close_stream[failure:close_stream.index("for (auto& buffer", failure)]
        self.assertNotIn("munmap", failure_body)
        self.assertNotIn("close(fd_)", failure_body)
        self.assertNotIn("camera_device_.Release()", failure_body)


if __name__ == "__main__":
    unittest.main()
