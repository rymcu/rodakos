from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from capture_release_stability import Evidence, SerialEvidenceStream


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

    def test_health_without_device_uptime_cannot_pass(self):
        evidence = Evidence()
        for index in range(960):
            evidence.accept(line().replace("I (0) ", ""), index * 30)
        report = evidence.report(28800, 28800, True)
        self.assertEqual(report["status"], "no-go")
        self.assertIn("missing_device_uptime", report["failures"])
        self.assertEqual(report["health_samples"], 0)

    def test_repeated_uptime_cannot_count_as_fresh_eight_hour_evidence(self):
        evidence = Evidence()
        for index in range(960):
            evidence.accept(line(30000), index * 30)
        report = evidence.report(28800, 28800, True)
        self.assertEqual(report["status"], "no-go")
        self.assertIn("uptime_repeated", report["failures"])
        self.assertEqual(report["health_samples"], 1)

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

    def test_serial_fragments_and_adjacent_lines_preserve_health_samples(self):
        evidence = Evidence()
        stream = SerialEvidenceStream(evidence)
        payload = (line(1000) + "\r\n" + line(31000) + "\n").encode()
        stream.accept(payload[:30], 0)
        self.assertEqual(evidence.samples, 0)
        stream.accept(payload[30:], 30)
        self.assertEqual(evidence.samples, 2)
        self.assertEqual(evidence.last_uptime, 31000)
        self.assertFalse(evidence.failures)

    def test_unterminated_runtime_failure_invalidates_complete_soak(self):
        for failure in ("Guru Meditation Error", "rst:0x1", "CORRUPT HEAP", "abort() was called"):
            with self.subTest(failure=failure):
                evidence = Evidence()
                stream = SerialEvidenceStream(evidence)
                for index in range(960):
                    stream.accept((line(index * 30000) + "\n").encode(), index * 30)
                stream.accept(failure[:4].encode(), 28798)
                stream.accept(failure[4:].encode(), 28799)
                stream.finish(28800)
                report = evidence.report(28800, 28800, True)
                self.assertEqual(report["status"], "no-go")
                self.assertIn("reset_or_runtime_failure", report["failures"])

    def test_unterminated_partial_health_is_not_silently_discarded(self):
        evidence = Evidence()
        stream = SerialEvidenceStream(evidence)
        stream.accept(b"I (30000) UnifiedMqtt: MQTT health: connected=1 internal_free=40000", 30)
        stream.finish(31)
        self.assertIn("missing_release_health_fields", evidence.failures)
        self.assertEqual(evidence.samples, 0)

    def test_final_flush_does_not_duplicate_the_last_health_sample(self):
        evidence = Evidence()
        stream = SerialEvidenceStream(evidence)
        stream.accept(line(1000).encode(), 1)
        stream.finish(2)
        stream.finish(3)
        self.assertEqual(evidence.samples, 1)
        self.assertFalse(evidence.failures)

    def test_unframed_serial_overflow_remains_no_go_after_recovery(self):
        evidence = Evidence()
        stream = SerialEvidenceStream(evidence)
        for _ in range(17):
            stream.accept(b"x" * 4096, 0)
        stream.accept((line(30000) + "\n").encode(), 30)
        report = evidence.report(30, 28800)
        self.assertEqual(report["status"], "no-go")
        self.assertIn("unframed_serial_overflow", report["failures"])
        self.assertEqual(report["health_samples"], 1)

    def test_failed_app_completion_invalidates_an_accepted_exercise(self):
        evidence = Evidence()
        for index in range(960):
            evidence.accept(line(index * 30000), index * 30)
        evidence.exercise_requests = 1
        evidence.accept('RODAK_APP_LAUNCH_RESULT {"queued":true}', 28780)
        stream = SerialEvidenceStream(evidence)
        stream.accept(b'RODAK_APP_LAUNCH_COMPLETE {"ok":false}', 28781)
        stream.finish(28800)
        report = evidence.report(28800, 28800, True)
        self.assertEqual(report["status"], "no-go")
        self.assertIn("app_launch_failed", report["failures"])
        self.assertEqual(report["exercise_completions"], 0)

    def test_every_accepted_exercise_requires_successful_completion(self):
        evidence = Evidence()
        for index in range(960):
            evidence.accept(line(index * 30000), index * 30)
        evidence.exercise_requests = 1
        evidence.accept('RODAK_APP_LAUNCH_RESULT {"queued":true}', 28780)
        report = evidence.report(28800, 28800, True)
        self.assertEqual(report["status"], "no-go")
        self.assertIn("uncompleted_app_launch", report["failures"])
        evidence.accept('RODAK_APP_LAUNCH_COMPLETE {"ok":true}', 28781)
        report = evidence.report(28800, 28800, True)
        self.assertEqual(report["status"], "pass-observed")
        self.assertEqual(report["exercise_completions"], 1)

    def test_voice_and_main_health_are_kept_in_resource_minima(self):
        evidence = Evidence()
        evidence.accept(
            "I (100) VoiceWakeService: Voice health: enabled=1 status=1 listening=1 "
            "internal_free=1000 internal_min=275 internal_largest=9000 "
            "psram_free=200000 psram_min=150000 psram_largest=120000 "
            "supervisor_stack_min_free=1796",
            1,
        )
        evidence.accept(
            "I (200) RodakOS: Main health: stack_min_free=2640 internal_free=8000 "
            "internal_largest=3584 dma_free=5000 dma_largest=3584",
            2,
        )
        report = evidence.report(2, 28800)
        self.assertEqual(report["resource_samples"]["voice"], 1)
        self.assertEqual(report["resource_samples"]["main"], 1)
        self.assertEqual(report["minima"]["internal_min"], 275)
        self.assertEqual(report["minima"]["internal_largest"], 3584)
        self.assertEqual(report["minima"]["dma_largest"], 3584)
        self.assertIn("insufficient_memory_or_stack_headroom", report["failures"])

    def test_warning_is_recorded_without_becoming_a_runtime_failure(self):
        evidence = Evidence()
        evidence.accept("W (100) Settings: Open NVS namespace home failed: ESP_ERR_NVS_NOT_FOUND", 1)
        evidence.accept("E:RX:153600-88320", 1.5)
        report = evidence.report(1, 28800)
        self.assertEqual(report["log_counts"]["warning"], 1)
        self.assertEqual(report["log_counts"]["error"], 1)
        self.assertNotIn("reset_or_runtime_failure", report["failures"])
        self.assertIn("error_log_present", report["failures"])

    def test_app_completion_before_ack_is_rejected(self):
        evidence = Evidence()
        evidence.record_app_request("home", 1)
        evidence.accept('RODAK_APP_LAUNCH_COMPLETE {"ok":true}', 2)
        evidence.accept('RODAK_APP_LAUNCH_RESULT {"queued":true}', 3)
        report = evidence.report(3, 28800, True)
        self.assertEqual(report["status"], "no-go")
        self.assertIn("app_launch_completion_before_ack", report["failures"])

    def test_app_request_ack_completion_stays_one_to_one(self):
        evidence = Evidence()
        for index in range(960):
            evidence.accept(line(index * 30000), index * 30)
        for index, app in enumerate(("home", "photos", "camera", "home", "music")):
            evidence.record_app_request(app, index * 300)
            evidence.accept('RODAK_APP_LAUNCH_RESULT {"queued":true}', index * 300 + 1)
            evidence.accept('RODAK_APP_LAUNCH_COMPLETE {"ok":true}', index * 300 + 2)
        report = evidence.report(28800, 28800, True)
        self.assertEqual(report["status"], "pass-observed")
        self.assertEqual(report["exercise_requests"], 5)
        self.assertEqual(report["exercise_acks"], 5)
        self.assertEqual(report["exercise_completions"], 5)


if __name__ == "__main__":
    unittest.main()
