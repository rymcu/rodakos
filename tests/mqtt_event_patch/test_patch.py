"""Provenance and lifecycle-hook checks for the generated MQTT overlay."""

import importlib.util
import os
from pathlib import Path
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("mqtt_patch", ROOT / "tools/prepare_mqtt_event_patch.py")
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class MqttPatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.component = self.directory / "component"
        self.idf = self.directory / "idf"
        self.output = self.directory / "generated"
        self.lock = self.directory / "dependencies.lock"
        self.manifest = self.directory / "manifest.yml"
        original = ROOT / "managed_components/espressif__mqtt"
        for relative in ("mqtt_client.c", "lib/include/mqtt_client_priv.h", "include/mqtt_client.h",
                         "lib/include/mqtt_config.h", "idf_component.yml", ".component_hash"):
            target = self.component / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original / relative, target)
        configured = os.environ.get("RODAKOS_IDF_PATH") or os.environ.get("IDF_PATH")
        if not configured:
            self.fail("Set RODAKOS_IDF_PATH to the reviewed ESP-IDF 6.0.2 source tree")
        event = self.idf / "components/esp_event/esp_event.c"
        event.parent.mkdir(parents=True)
        shutil.copyfile(Path(configured) / "components/esp_event/esp_event.c", event)
        shutil.copyfile(ROOT / "dependencies.lock", self.lock)
        shutil.copyfile(ROOT / "main/idf_component.yml", self.manifest)

    def prepare(self):
        return PATCH.prepare(self.component, self.lock, self.manifest, self.idf, self.output)

    def test_generates_idempotently_without_changing_managed_inputs(self):
        before = {path.relative_to(self.component): path.read_bytes()
                  for path in self.component.rglob("*") if path.is_file()}
        self.assertTrue(self.prepare())
        timestamp = (self.output / "mqtt_client.c").stat().st_mtime_ns
        self.assertFalse(self.prepare())
        self.assertEqual(timestamp, (self.output / "mqtt_client.c").stat().st_mtime_ns)
        self.assertEqual(before, {path.relative_to(self.component): path.read_bytes()
                                 for path in self.component.rglob("*") if path.is_file()})

    def test_rejects_each_reviewed_source_drift_before_output(self):
        for relative in ("mqtt_client.c", "lib/include/mqtt_client_priv.h", "include/mqtt_client.h",
                         "lib/include/mqtt_config.h", "idf_component.yml"):
            with self.subTest(relative=relative):
                path = self.component / relative
                original = path.read_bytes()
                path.write_bytes(original + b"\n/* changed */\n")
                with self.assertRaisesRegex(ValueError, "Unreviewed MQTT"):
                    self.prepare()
                self.assertFalse(self.output.exists())
                path.write_bytes(original)

    def test_rejects_event_loop_semantic_drift(self):
        path = self.idf / "components/esp_event/esp_event.c"
        path.write_bytes(path.read_bytes() + b"\n/* changed */\n")
        with self.assertRaisesRegex(ValueError, "Unreviewed MQTT"):
            self.prepare()

    def test_rejects_lock_and_project_version_drift(self):
        for path in (self.lock, self.manifest):
            with self.subTest(path=path.name):
                original = path.read_text()
                path.write_text(original.replace("espressif/mqtt:", "espressif/unreviewed_mqtt:"))
                with self.assertRaises(ValueError):
                    self.prepare()
                path.write_text(original)

    def test_rejects_managed_package_identity_drift(self):
        (self.component / ".component_hash").write_text("unreviewed")
        with self.assertRaisesRegex(ValueError, "package hash"):
            self.prepare()

    def test_refuses_to_overwrite_managed_component(self):
        self.output = self.component / "overlay"
        with self.assertRaisesRegex(ValueError, "outside managed"):
            self.prepare()

    def test_accepts_line_ending_conversion_only(self):
        for path in self.component.rglob("*"):
            if path.is_file():
                path.write_bytes(PATCH.read_lf(path).replace(b"\n", b"\r\n"))
        self.assertTrue(self.prepare())

    def test_cleans_queue_at_start_stop_and_destroy_and_requires_locks(self):
        self.prepare()
        source = (self.output / "mqtt_client.c").read_text()
        start = source[source.index("esp_err_t esp_mqtt_client_start("):
                       source.index("esp_err_t esp_mqtt_client_disconnect(")]
        task = source[source.index("static void esp_mqtt_task(void *pv)\n"):
                      source.index("esp_err_t esp_mqtt_client_start(")]
        destroy = source[source.index("esp_err_t esp_mqtt_client_destroy("):
                         source.index("static char *create_string(const char *ptr, int len)\n{")]
        self.assertIn("rodak_mqtt_reset_custom_events(client);", start)
        self.assertLess(task.index("rodak_mqtt_reset_custom_events(client);"),
                        task.index("xEventGroupSetBits(client->status_bits, STOPPED_BIT);"))
        self.assertIn("vQueueDelete(client->rodak_custom_events);", destroy)
        self.assertIn('#error "Rodak MQTT event overlay requires API locks"', source)
        self.assertNotIn("queued_events", source)


if __name__ == "__main__":
    unittest.main()
