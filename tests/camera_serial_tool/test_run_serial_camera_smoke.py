import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import run_serial_camera_smoke as tool
from capture_first_boot import REQUIRED_MARKERS


def health(source, uptime, connected=1):
    fields = {
        "MQTT": f"connected={connected} telemetry_queued=1 stack_min_free=2048 internal_free=40000 "
                "internal_largest=16384 psram_free=3000000 psram_largest=2097152",
        "Main": "stack_min_free=2048 internal_free=40000 internal_largest=16384",
        "Voice": "enabled=1 listening=1 supervisor_stack_bytes=6144 "
                 "supervisor_stack_min_free=4432 internal_free=40000 internal_largest=16384",
    }
    return f"I ({uptime}) Runtime: {source} health: {fields[source]}"


class Clock:
    def __init__(self):
        self.now = 0

    def __call__(self):
        return self.now


class FakePort:
    """Only an in-memory byte stream; it has no open method or OS serial dependency."""
    def __init__(self, clock, mode=None):
        self.clock = clock
        self.mode = mode
        self.buffer = bytearray()
        self.writes = []
        self.health_due = []
        self.camera_active = False
        self.interrupt_pending = False
        self.delayed_completion = None
        self.reset_requests = 0

    def reset_input_buffer(self):
        self.reset_requests += 1
        raise AssertionError("A smoke session must not reset or discard serial evidence")

    @property
    def in_waiting(self):
        return len(self.buffer)

    def flush(self):
        pass

    def enqueue(self, messages):
        uptime = int(self.clock() * 1000)
        lines = [message if message.startswith("RODAK_") else f"I ({uptime + index * 10}) {message}"
                 for index, message in enumerate(messages)]
        self.buffer.extend(("\n".join(lines) + "\n").encode())

    def write(self, data):
        self.writes.append(data)
        app = data.decode().strip().removeprefix("RODAK_APP_LAUNCH_V1 ")
        messages = ['RODAK_APP_LAUNCH_RESULT {"queued":true}', f"PhoneAppHost: Launching app: {app}"]
        self.health_due = []
        if app == "camera":
            self.camera_active = True
            messages += ["CameraApp: Camera app created; preview startup deferred",
                         "PhoneAppHost: App launched: camera", 'RODAK_APP_LAUNCH_COMPLETE {"ok":true}']
            if self.mode != "missing_frame":
                messages += ["CameraApp: Camera preview image updated: sequence=1"]
            if self.mode == "interrupt":
                self.interrupt_pending = True
        else:
            leaving_camera = self.camera_active
            if self.camera_active:
                messages += ["PhoneAppHost: Closing app: camera", "CameraApp: Destroy: capture guard begin"]
                for marker in tool.CLOSE_MARKERS:
                    if self.mode == "deferred" and "device release complete" in marker:
                        messages += ["CameraService: CloseStream: device release deferred for retry: ESP_FAIL"]
                    else:
                        messages += [marker + " frames=1" if marker.endswith("stopped:") else marker]
                self.camera_active = False
            messages += ["PhoneAppHost: App launched: home", 'RODAK_APP_LAUNCH_COMPLETE {"ok":true}']
            if leaving_camera and self.mode in ("delayed_home", "missing_home"):
                messages.pop()
                if self.mode == "delayed_home":
                    self.delayed_completion = self.clock() + 3
            self.health_due = [self.clock() + delay for delay in (5, 35, 65)]
        self.enqueue(messages)

    def read(self, size):
        self.clock.now += 0.25
        if self.interrupt_pending:
            self.interrupt_pending = False
            raise KeyboardInterrupt()
        if self.buffer:
            chunk = bytes(self.buffer[:size])
            del self.buffer[:size]
            return chunk
        if self.delayed_completion is not None and self.clock() >= self.delayed_completion:
            self.delayed_completion = None
            self.enqueue(['RODAK_APP_LAUNCH_COMPLETE {"ok":true}'])
        if self.health_due and self.clock() >= self.health_due[0]:
            self.health_due.pop(0)
            sources = ("MQTT", "Main") if self.mode == "missing_voice" else ("MQTT", "Main", "Voice")
            self.buffer.extend(("\n".join(health(source, int(self.clock() * 1000),
                                                 connected=0 if self.mode == "disconnected" else 1)
                                            for source in sources) + "\n").encode())
        return b""


class CameraSmokeTests(unittest.TestCase):
    def run_trace(self, mode=None, cycles=1):
        clock = Clock()
        port = FakePort(clock, mode)
        log = io.BytesIO()
        with patch.object(tool.time, "monotonic", clock):
            session = tool.CameraSmokeSession(port, log)
            error = None
            try:
                session.run(cycles, 2, 65)
            except KeyboardInterrupt:
                error = "KeyboardInterrupt"
            try:
                session.cleanup_home(2)
            except TimeoutError as failure:
                session.cleanup["error"] = str(failure)
            session.finish()
            report = session.report(cycles, error)
        return report, port, log.getvalue()

    def test_two_cycles_use_only_camera_home_commands_and_remain_short_software_evidence(self):
        report, port, raw = self.run_trace(cycles=2)
        self.assertEqual(report["status"], "software-smoke-observed", report)
        self.assertEqual(port.writes, [b"RODAK_APP_LAUNCH_V1 home\n",
                                      b"RODAK_APP_LAUNCH_V1 camera\n", b"RODAK_APP_LAUNCH_V1 home\n",
                                      b"RODAK_APP_LAUNCH_V1 camera\n", b"RODAK_APP_LAUNCH_V1 home\n"])
        self.assertFalse(report["eight_hour_gate_passed"])
        self.assertEqual(report["resources"]["status"], "incomplete")
        self.assertTrue(report["cleanup"]["home_confirmed"])
        self.assertTrue(all(cycle["health"]["duration_seconds"] >= 60 for cycle in report["cycles"]))
        self.assertIn(b"Camera preview image updated", raw)

    def test_missing_frame_returns_home_once_and_keeps_the_failure(self):
        report, port, _ = self.run_trace("missing_frame", cycles=3)
        self.assertEqual(report["status"], "no-go")
        self.assertEqual(len(report["cycles"]), 1)
        self.assertEqual(len(port.writes), 3)
        self.assertEqual(port.reset_requests, 0)
        self.assertTrue(report["cleanup"]["home_confirmed"])
        self.assertIn("Camera preview submission", " ".join(report["failures"]))

    def test_delayed_home_cleanup_waits_for_the_original_request_without_replay(self):
        report, port, _ = self.run_trace("delayed_home", cycles=3)
        self.assertEqual(report["status"], "no-go")
        self.assertIn("Home navigation after Camera", " ".join(report["failures"]))
        self.assertTrue(report["cleanup"]["home_confirmed"])
        self.assertFalse(report["cleanup"]["additional_request"])
        self.assertEqual(len(port.writes), 3)
        self.assertEqual(port.reset_requests, 0)

    def test_missing_home_keeps_cleanup_failure_and_never_retries_the_command(self):
        report, port, _ = self.run_trace("missing_home", cycles=3)
        self.assertEqual(report["status"], "no-go")
        self.assertFalse(report["cleanup"]["home_confirmed"])
        self.assertIn("bounded Home cleanup", report["cleanup"]["error"])
        self.assertEqual(len(port.writes), 3)

    def test_deferred_release_is_not_a_complete_close_and_does_not_start_next_camera(self):
        report, port, raw = self.run_trace("deferred", cycles=3)
        self.assertEqual(report["status"], "no-go")
        self.assertEqual(len(port.writes), 3)
        self.assertEqual(len(report["cycles"][0]["close_markers"]), 2)
        self.assertFalse(report["cycles"][0]["software_path_observed"])
        self.assertIn(b"deferred for retry", raw)

    def test_health_failure_stays_no_go_after_home_and_stops_future_cycles(self):
        for mode, failure in (("missing_voice", "missing_fresh_voice_health"),
                              ("disconnected", "mqtt_disconnected_or_publish_failed")):
            with self.subTest(mode=mode):
                report, port, _ = self.run_trace(mode, cycles=3)
                self.assertEqual(report["status"], "no-go")
                self.assertIn(failure, report["failures"])
                self.assertEqual(len(port.writes), 3)
                self.assertTrue(report["cleanup"]["home_confirmed"])

    def test_interrupt_uses_the_same_fake_session_for_home_and_never_erases_original_failure(self):
        report, port, _ = self.run_trace("interrupt", cycles=3)
        self.assertEqual(report["status"], "no-go")
        self.assertIn("KeyboardInterrupt", report["failures"])
        self.assertTrue(report["cleanup"]["home_confirmed"])
        self.assertEqual(port.writes[-1], b"RODAK_APP_LAUNCH_V1 home\n")
        self.assertEqual(len(port.writes), 3)
        self.assertEqual(port.reset_requests, 0)

    def test_old_or_reordered_close_logs_cannot_complete_this_instance(self):
        old = [(1, f"I (100) {marker}") for marker in tool.CLOSE_MARKERS]
        self.assertEqual(tool.close_evidence(old, 1000), [])
        reordered = [(2, f"I ({1100 + index}) {marker}")
                     for index, marker in enumerate(reversed(tool.CLOSE_MARKERS))]
        self.assertLess(len(tool.close_evidence(reordered, 1000)), len(tool.CLOSE_MARKERS))

    def test_health_requires_each_source_and_fresh_uptime_after_home(self):
        entries = [(30, health(source, 30000)) for source in tool.HEALTH_FIELDS]
        entries += [(60, health(source, 60000)) for source in tool.HEALTH_FIELDS]
        self.assertTrue(tool.health_evidence(entries, 0, 65, 1000)["software_health_observed"])
        self.assertIn("post_home_observation_under_60_seconds", tool.health_evidence(entries, 0, 59, 1000)["failures"])
        self.assertIn("missing_fresh_mqtt_health", tool.health_evidence(entries, 0, 65, 60000)["failures"])
        repeated = entries + [(61, health("Voice", 60000))]
        self.assertIn("stale_voice_health", tool.health_evidence(repeated, 0, 65, 1000)["failures"])

    def test_low_stack_and_trailing_panic_cannot_be_hidden(self):
        report, _, _ = self.run_trace()
        self.assertEqual(report["status"], "software-smoke-observed")
        entries = [(30, health(source, 30000).replace("supervisor_stack_min_free=4432", "supervisor_stack_min_free=128"))
                   for source in tool.HEALTH_FIELDS]
        entries += [(60, health(source, 60000)) for source in tool.HEALTH_FIELDS]
        self.assertIn("insufficient_voice_supervisor_stack_headroom",
                      tool.health_evidence(entries, 0, 65, 1000)["failures"])
        clock = Clock()
        with patch.object(tool.time, "monotonic", clock):
            session = tool.CameraSmokeSession(FakePort(clock), io.BytesIO())
            session.pending.extend(b"Guru Meditation Error")
            session.finish()
            self.assertIn("reset_or_runtime_failure", session.report(1)["failures"])


class PackageContextTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.manifest = {"buildFlavor": "production", "releaseFaultInjection": False,
                         "homeHardwareTestPopulation": False, "fileName": "rodakos.bin",
                         "taskNo": "smoke-fixture", "version": "0.1.2-dev.1", "checksumValue": "a" * 64}
        for name in ("rodakos.bin", "rodakos_recovery.bin", "ota-public.pem"):
            (self.root / name).write_text("mocked verification input")
        self.flash = self.root / "flash.log"
        self.boot = self.root / "boot.log"
        self.flash.write_text("operator-supplied verified flash record")
        self.boot.write_text("\n".join(REQUIRED_MARKERS))
        self.write_manifest()

    def tearDown(self):
        self.temporary.cleanup()

    def write_manifest(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest))

    def test_existing_verifiers_are_used_without_claiming_installed_image_identity(self):
        with patch.object(tool.subprocess, "run") as verify:
            context = tool.package_context(self.root, self.flash, self.boot)
        self.assertEqual(verify.call_args.args[0][-3:], ["verify-package", "--directory", str(self.root.resolve())])
        self.assertIn("no installed-image readback", context["identity_boundary"])
        self.assertFalse(context["installed_image_verified"])
        self.assertFalse(context["binding_verified"])

    def test_test_flavors_missing_files_and_bad_boot_stop_before_any_serial_action(self):
        for field, value in (("buildFlavor", "release-fault-test"), ("releaseFaultInjection", True),
                             ("homeHardwareTestPopulation", True)):
            with self.subTest(field=field), patch.object(tool.subprocess, "run") as verify:
                original = self.manifest[field]
                self.manifest[field] = value
                self.write_manifest()
                with self.assertRaises(ValueError):
                    tool.package_context(self.root, self.flash, self.boot)
                verify.assert_not_called()
                self.manifest[field] = original
                self.write_manifest()
        with self.assertRaises(ValueError):
            tool.package_context(self.root, self.root / "missing.log", self.boot)
        self.manifest["fileName"] = "missing.bin"
        self.write_manifest()
        with self.assertRaises(ValueError):
            tool.package_context(self.root, self.flash, self.boot)
        self.manifest["fileName"] = "rodakos.bin"
        self.write_manifest()
        self.boot.write_text("old or incomplete boot")
        with self.assertRaises(ValueError):
            tool.package_context(self.root, self.flash, self.boot)

    def test_signature_verification_failure_propagates(self):
        with patch.object(tool.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "verify-package")):
            with self.assertRaises(subprocess.CalledProcessError):
                tool.package_context(self.root, self.flash, self.boot)


if __name__ == "__main__":
    unittest.main()
