"""验证生产 emitter 的有界输出，不实现现场串口收集器。"""
import json
import re
import struct
import subprocess
import sys
import unittest

DRIVER = sys.argv.pop(1)
PREFIX = "RODAK_VOICE_PREPARE_TRACE "


class DiagnosticOutputTest(unittest.TestCase):
    def run_scenario(self, name):
        result = subprocess.run([DRIVER, name], capture_output=True, check=True, timeout=5)
        self.assertEqual(result.stderr, b"")
        lines = result.stdout.decode("ascii").splitlines()
        self.assertTrue(lines)
        self.assertTrue(all(line.startswith(PREFIX) for line in lines))
        return [json.loads(line[len(PREFIX):]) for line in lines]

    def assert_record(self, records, scope, stages):
        self.assertEqual(len(records), len(stages) + 2)
        header, *samples, footer = records
        self.assertEqual(header["phase"], "snapshot")
        self.assertIs(header["ok"], True)
        self.assertEqual(header["scope_id"], scope)
        self.assertEqual(header["count"], len(stages))
        self.assertEqual(header["flags"], 0)
        self.assertEqual(header["rejected_begin_count"], 2**32 - 1)
        self.assertIs(header["open_acquired"], stages[-1] == "open_released")
        self.assertEqual(header["self_handle"], 2 ** (struct.calcsize("P") * 8) - 1)
        self.assertIsNotNone(re.fullmatch(r"(?:[0-9a-f]{2}){0,16}", header["task_name_hex"]))
        for index, (sample, stage) in enumerate(zip(samples, stages)):
            self.assertEqual(sample["phase"], "sample")
            self.assertEqual(sample["scope_id"], scope)
            self.assertEqual(sample["index"], index)
            self.assertEqual(sample["stage"], stage)
            self.assertEqual(sample["self_handle"], header["self_handle"])
            self.assertEqual(sample["effective_priority"], 4 + index)
            self.assertEqual(sample["before_us"], 9007199254740993 + 10 * index)
            self.assertEqual(sample["after_us"], sample["before_us"] + 1)
        self.assertEqual(footer, {"phase": "complete", "ok": True,
                                  "scope_id": scope, "count": len(stages)})

    def test_success_and_early_exit_records_are_complete(self):
        cases = {
            "full": ["prepare_begin", "open_acquired", "cloud_returned", "open_released"],
            "cancel": ["prepare_begin", "open_acquired", "open_released"],
            "no_open": ["prepare_begin", "exit_no_open"],
        }
        for scenario, stages in cases.items():
            with self.subTest(scenario=scenario):
                self.assert_record(self.run_scenario(scenario), 11, stages)

    def test_unavailable_and_consumed_records_are_not_repeated(self):
        unavailable = {"phase": "unavailable", "ok": False}
        self.assertEqual(self.run_scenario("unavailable"), [unavailable])
        records = self.run_scenario("consume")
        self.assert_record(records[:-1], 13,
                           ["prepare_begin", "open_acquired", "cloud_returned", "open_released"])
        self.assertEqual(records[-1], unavailable)

    def test_invalid_count_does_not_emit_a_partial_snapshot(self):
        self.assertEqual(self.run_scenario("invalid"), [
            {"phase": "invalid_snapshot", "ok": False},
            {"phase": "unavailable", "ok": False},
        ])

    def test_arbitrary_task_name_bytes_stay_inside_hex(self):
        records = self.run_scenario("name")
        self.assert_record(records, 12,
                           ["prepare_begin", "open_acquired", "cloud_returned", "open_released"])
        self.assertEqual(bytes.fromhex(records[0]["task_name_hex"]),
                         bytes([34, 92, 10, 13, 9, 1, 127, 128, 255, 37, 115, 123, 125, 91, 93, 33]))

    def test_two_records_keep_their_own_scope_and_counts(self):
        records = self.run_scenario("records")
        self.assertEqual(len(records), 10)
        self.assert_record(records[:6], 101,
                           ["prepare_begin", "open_acquired", "cloud_returned", "open_released"])
        self.assert_record(records[6:], 202, ["prepare_begin", "exit_no_open"])


if __name__ == "__main__":
    unittest.main()
