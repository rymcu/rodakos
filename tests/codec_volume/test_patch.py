"""The source overlay must remain reproducible and reject an unreviewed dependency."""

import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "prepare_codec_volume_patch", ROOT / "tools" / "prepare_codec_volume_patch.py"
)
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class CodecPatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.component = self.root / "managed" / "codec"
        self.component.mkdir(parents=True)
        for name in ("esp_codec_dev.c", "idf_component.yml", ".component_hash"):
            shutil.copyfile(ROOT / "managed_components" / "espressif__esp_codec_dev" / name,
                            self.component / name)
        self.lock = self.root / "dependencies.lock"
        self.manifest = self.root / "idf_component.yml"
        shutil.copyfile(ROOT / "dependencies.lock", self.lock)
        shutil.copyfile(ROOT / "main" / "idf_component.yml", self.manifest)
        self.output = self.root / "build" / "esp_codec_dev.c"

    def generate(self):
        return PATCH.prepare(self.component, self.lock, self.manifest, self.output)

    def replace(self, path, before, after):
        value = path.read_text(encoding="utf-8")
        self.assertIn(before, value)
        path.write_text(value.replace(before, after, 1), encoding="utf-8")

    def assert_refused(self):
        with self.assertRaises(ValueError):
            self.generate()
        self.assertFalse(self.output.exists())

    def test_first_generation_leaves_managed_source_intact(self):
        original = (self.component / "esp_codec_dev.c").read_bytes()
        self.assertTrue(self.generate())
        self.assertEqual((self.component / "esp_codec_dev.c").read_bytes(), original)
        self.assertNotEqual(self.output.read_bytes(), original.replace(b"\r\n", b"\n"))

    def test_repeat_generation_is_identical_without_touching_mtime(self):
        self.generate()
        original = self.output.read_bytes()
        mtime = self.output.stat().st_mtime_ns
        self.assertFalse(self.generate())
        self.assertEqual(self.output.read_bytes(), original)
        self.assertEqual(self.output.stat().st_mtime_ns, mtime)

    def test_deleted_generated_source_is_recreated(self):
        self.generate()
        original = self.output.read_bytes()
        self.output.unlink()
        self.assertTrue(self.generate())
        self.assertEqual(self.output.read_bytes(), original)

    def test_source_drift_fails_before_writing(self):
        self.replace(self.component / "esp_codec_dev.c", "(50)", "(51)")
        self.assert_refused()

    def test_source_drift_rejects_existing_output_without_overwriting_it(self):
        self.generate()
        original = self.output.read_bytes()
        self.replace(self.component / "esp_codec_dev.c", "(50)", "(51)")
        with self.assertRaises(ValueError):
            self.generate()
        self.assertEqual(self.output.read_bytes(), original)

    def test_component_manifest_version_drift_is_rejected(self):
        self.replace(self.component / "idf_component.yml", "version: 1.5.7", "version: 1.5.8")
        self.assert_refused()

    def test_project_version_drift_is_rejected(self):
        self.replace(self.manifest, 'esp_codec_dev: "1.5.7"', 'esp_codec_dev: "1.5.8"')
        self.assert_refused()

    def test_locked_version_drift_is_rejected(self):
        self.replace(self.lock, "version: 1.5.7", "version: 1.5.8")
        self.assert_refused()

    def test_locked_package_hash_drift_is_rejected(self):
        self.replace(self.lock, "54870f7a66dd93367b4153dedb22299efadf8b7b2ead322cdebc7c92a542fa35",
                     "0" * 64)
        self.assert_refused()

    def test_locked_source_drift_is_rejected(self):
        value = self.lock.read_text(encoding="utf-8")
        before, codec = value.split("  espressif/esp_codec_dev:\n", 1)
        codec = codec.replace("registry_url: https://components.espressif.com/",
                              "registry_url: https://unreviewed.invalid/", 1)
        self.lock.write_text(before + "  espressif/esp_codec_dev:\n" + codec, encoding="utf-8")
        self.assert_refused()

    def test_managed_package_identity_drift_is_rejected(self):
        (self.component / ".component_hash").write_text("0" * 64, encoding="utf-8")
        self.assert_refused()

    def test_overlay_cannot_overwrite_managed_source(self):
        original = (self.component / "esp_codec_dev.c").read_bytes()
        self.output = self.component / "esp_codec_dev.c"
        with self.assertRaises(ValueError):
            self.generate()
        self.assertEqual(self.output.read_bytes(), original)

    def test_checkout_line_endings_do_not_change_generated_bytes(self):
        self.generate()
        original = self.output.read_bytes()
        for name in ("esp_codec_dev.c", "idf_component.yml"):
            path = self.component / name
            path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        self.assertFalse(self.generate())
        self.assertEqual(self.output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
