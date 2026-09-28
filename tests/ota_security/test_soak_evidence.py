from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from capture_release_stability import Evidence


def line(timestamp=0, free=40000):
    return (f"I ({timestamp}) UnifiedMqtt: MQTT health: connected=1 internal_free={free} "
            "internal_largest=16384 stack_min_free=2048 psram_free=3000000 "
            "psram_largest=2097152 telemetry_queued=1")


class SoakTests(unittest.TestCase):
    def test_only_complete_eight_hour_capture_passes(self):
        evidence = Evidence()
        for index in range(960):
            evidence.accept(line(index * 30000), index * 30)
        self.assertEqual(evidence.report(28800, 28800, True)["status"], "pass-observed")
        self.assertEqual(evidence.report(100, 100, True)["status"], "incomplete")
        self.assertEqual(evidence.report(28800, 28800, False)["status"], "incomplete")

    def test_silence_and_legacy_logs_cannot_pass(self):
        self.assertEqual(Evidence().report(28800, 28800, True)["status"], "no-go")
        evidence = Evidence()
        evidence.accept("MQTT health: connected=1 internal_free=123", 0)
        self.assertIn("missing_release_health_fields", evidence.report(1, 28800)["failures"])

    def test_reset_gap_and_heap_drop_are_failures(self):
        evidence = Evidence()
        for index in range(10):
            evidence.accept(line(1000 + index * 30000), index * 30)
        for index in range(10, 20):
            evidence.accept(line(index * 30000, 20000), index * 30)
        evidence.accept(line(1, 20000), 1000)
        failures = evidence.report(1000, 28800)["failures"]
        self.assertIn("uptime_regressed", failures)
        self.assertIn("health_gap_over_90_seconds", failures)
        self.assertIn("internal_heap_median_drop_over_8KiB", failures)


if __name__ == "__main__":
    unittest.main()
