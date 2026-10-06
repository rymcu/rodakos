"""Fail-closed provenance, source isolation, and regeneration checks for the WS overlay."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("ws_patch", ROOT / "tools/prepare_websocket_redirect_patch.py")
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)
PROVENANCE = json.loads((PATCH.PATCH_DIR / "provenance.json").read_text())


class WebSocketPatchTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.component = self.directory / "component"
        self.idf = self.directory / "idf"
        self.output = self.directory / "generated"
        self.lock = self.directory / "dependencies.lock"
        self.manifest = self.directory / "manifest.yml"
        original = ROOT / "managed_components/espressif__esp_websocket_client"
        for relative in [*PROVENANCE["source_hashes_lf"], ".component_hash"]:
            target = self.component / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original / relative, target)
        configured = os.environ.get("RODAKOS_IDF_PATH") or os.environ.get("IDF_PATH")
        if not configured:
            self.fail("Set RODAKOS_IDF_PATH to the reviewed ESP-IDF 6.0.2 source tree")
        for relative in PROVENANCE["idf_source_hashes_lf"]:
            target = self.idf / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(Path(configured) / relative, target)
        shutil.copyfile(ROOT / "dependencies.lock", self.lock)
        shutil.copyfile(ROOT / "main/idf_component.yml", self.manifest)

    def prepare(self):
        return PATCH.prepare(self.component, self.lock, self.manifest, self.idf, self.output)

    def test_generation_is_idempotent_and_managed_sources_are_unchanged(self):
        before = {path.relative_to(self.component): path.read_bytes()
                  for path in self.component.rglob("*") if path.is_file()}
        self.assertTrue(self.prepare())
        generated = self.output / "esp_websocket_client.c"
        timestamp = generated.stat().st_mtime_ns
        self.assertFalse(self.prepare())
        self.assertEqual(timestamp, generated.stat().st_mtime_ns)
        self.assertEqual(before, {path.relative_to(self.component): path.read_bytes()
                                 for path in self.component.rglob("*") if path.is_file()})

    def test_rejects_each_component_source_metadata_and_license_drift(self):
        for relative in PROVENANCE["source_hashes_lf"]:
            with self.subTest(relative=relative):
                path = self.component / relative
                original = path.read_bytes()
                path.write_bytes(original + b"\nchanged\n")
                with self.assertRaisesRegex(ValueError, "Unreviewed WebSocket"):
                    self.prepare()
                self.assertFalse(self.output.exists())
                path.write_bytes(original)

    def test_rejects_each_idf_transport_or_version_source_drift(self):
        for relative in PROVENANCE["idf_source_hashes_lf"]:
            with self.subTest(relative=relative):
                path = self.idf / relative
                original = path.read_bytes()
                path.write_bytes(original + b"\nchanged\n")
                with self.assertRaisesRegex(ValueError, "Unreviewed WebSocket"):
                    self.prepare()
                self.assertFalse(self.output.exists())
                path.write_bytes(original)

    def test_rejects_each_locked_identity_field(self):
        original = PATCH.read_lf(self.lock).decode()
        start = original.index("  espressif/esp_websocket_client:\n")
        end = original.index("  espressif/led_strip:\n", start)
        block = original[start:end]
        for value in ("version: 1.8.0", PROVENANCE["component_hash"],
                      PROVENANCE["registry_url"], "type: service"):
            with self.subTest(value=value):
                self.lock.write_text(original[:start] + block.replace(value, "unreviewed") + original[end:])
                with self.assertRaises(ValueError):
                    self.prepare()
                self.assertFalse(self.output.exists())

    def test_rejects_changed_project_pin(self):
        self.manifest.write_text(self.manifest.read_text().replace(
            'espressif/esp_websocket_client: "1.8.0"', 'espressif/esp_websocket_client: "1.9.0"'))
        with self.assertRaisesRegex(ValueError, "reviewed pin"):
            self.prepare()

    def test_rejects_missing_or_ambiguous_lock_entry(self):
        original = PATCH.read_lf(self.lock).decode()
        self.lock.write_text(original.replace("espressif/esp_websocket_client:", "espressif/other_client:"))
        with self.assertRaisesRegex(ValueError, "missing or ambiguous"):
            self.prepare()
        start = original.index("  espressif/esp_websocket_client:\n")
        end = original.index("  espressif/led_strip:\n", start)
        self.lock.write_text(original[:end] + original[start:end] + original[end:])
        with self.assertRaisesRegex(ValueError, "missing or ambiguous"):
            self.prepare()

    def test_rejects_managed_component_hash_drift(self):
        (self.component / ".component_hash").write_text("unreviewed")
        with self.assertRaisesRegex(ValueError, "package hash"):
            self.prepare()

    def test_refuses_output_in_managed_component(self):
        for target in (self.component, self.component / "overlay"):
            with self.subTest(target=target):
                self.output = target
                with self.assertRaisesRegex(ValueError, "outside managed"):
                    self.prepare()

    def test_failure_preserves_last_verified_output(self):
        self.prepare()
        generated = self.output / "esp_websocket_client.c"
        before = generated.read_bytes()
        (self.component / "esp_websocket_client.c").write_text("unreviewed")
        with self.assertRaisesRegex(ValueError, "Unreviewed WebSocket"):
            self.prepare()
        self.assertEqual(before, generated.read_bytes())

    def test_accepts_line_ending_conversion_only(self):
        for root in (self.component, self.idf):
            for path in root.rglob("*"):
                if path.is_file():
                    path.write_bytes(PATCH.read_lf(path).replace(b"\n", b"\r\n"))
        self.assertTrue(self.prepare())

    def test_regenerates_missing_output(self):
        self.prepare()
        generated = self.output / "esp_websocket_client.c"
        before = generated.read_bytes()
        generated.unlink()
        self.assertTrue(self.prepare())
        self.assertEqual(before, generated.read_bytes())

    def test_only_redirect_branch_changes_and_tls_and_lifecycle_are_identical(self):
        self.prepare()
        original = PATCH.read_lf(self.component / "esp_websocket_client.c").decode()
        generated = (self.output / "esp_websocket_client.c").read_text()
        start, end = original.index(PATCH.REDIRECT_START), original.index(PATCH.REDIRECT_END)
        prefix, suffix = original[:start], original[end + len("#endif\n"):]
        self.assertTrue(generated.startswith(prefix))
        self.assertTrue(generated.endswith(suffix))
        branch = generated[len(prefix):-len(suffix)]
        self.assertNotIn("esp_transport_ws_get_redir_uri(", generated)
        self.assertNotIn("set_uri", branch)
        self.assertNotIn("free(", branch)
        self.assertIn("esp_transport_ws_get_upgrade_request_status", branch)
        self.assertIn("WEBSOCKET_ERROR_TYPE_HANDSHAKE", branch)
        self.assertIn("esp_websocket_client_abort_connection", branch)


if __name__ == "__main__":
    unittest.main()
