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


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\bint\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
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


if __name__ == "__main__":
    unittest.main()
