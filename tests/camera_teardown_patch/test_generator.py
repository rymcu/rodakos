"""Negative/immutability checks using the pinned production overlay inputs."""

import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from ruamel.yaml import YAML

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("camera_overlay", ROOT / "tools/prepare_camera_teardown_patch.py")
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)
IDF_PATH = None


class CameraGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.video = self.root / "managed_components/espressif__esp_video"
        self.sensor = self.root / "managed_components/espressif__esp_cam_sensor"
        self.idf = self.root / "idf"
        self.lock = self.root / "dependencies.lock"
        self.manifest = self.root / "main/idf_component.yml"
        self.header = self.root / "main/phone_os/camera-teardown-diagnostics.h"
        self.output = self.root / "build/generated"
        self.pinned_files = []
        provenance = json.loads((overlay.PATCH_DIR / "provenance.json").read_text())
        for component, destination in (("esp_video", self.video), ("esp_cam_sensor", self.sensor)):
            for name in [".component_hash", *provenance["components"][component]["source_hashes_lf"]]:
                self.copy(ROOT / "managed_components" / ("espressif__" + component) / name, destination / name)
        for name in provenance["idf_source_hashes_lf"]:
            self.copy(IDF_PATH / name, self.idf / name)
        for name, destination in (("dependencies.lock", self.lock), ("main/idf_component.yml", self.manifest),
                                  ("main/phone_os/camera-teardown-diagnostics.h", self.header)):
            self.copy(ROOT / name, destination)

    def tearDown(self):
        self.temporary.cleanup()

    def copy(self, source, destination):
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        self.pinned_files.append(destination)

    def prepare(self, output=None):
        return overlay.prepare(self.video, self.sensor, self.lock, self.manifest, self.idf,
                               self.header, output or self.output)

    def snapshots(self):
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in self.pinned_files}

    def test_idempotent_and_sources_unchanged(self):
        before = self.snapshots()
        self.assertTrue(self.prepare())
        generated = list(self.output.iterdir())
        self.assertEqual(len(generated), 2)
        timestamps = {path: path.stat().st_mtime_ns for path in generated}
        self.assertFalse(self.prepare())
        self.assertEqual(timestamps, {path: path.stat().st_mtime_ns for path in generated})
        self.assertEqual(before, self.snapshots())

    def test_every_reviewed_input_rejects_drift_before_output_change(self):
        self.prepare()
        expected = {path: path.read_bytes() for path in self.output.iterdir()}
        for path in self.pinned_files:
            with self.subTest(path=path.relative_to(self.root)):
                original = path.read_bytes()
                try:
                    path.write_bytes(original + b"\nUNREVIEWED\n")
                    with self.assertRaises(ValueError):
                        self.prepare()
                    self.assertEqual(expected, {output: output.read_bytes() for output in self.output.iterdir()})
                finally:
                    path.write_bytes(original)

    def test_no_partial_outputs_on_late_input_failure(self):
        path = self.idf / "tools/cmake/version.cmake"
        path.write_bytes(path.read_bytes() + b"\nunreviewed\n")
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_crlf_is_normalized(self):
        for path in self.pinned_files:
            path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        self.assertTrue(self.prepare())

    def test_rejects_outputs_in_reviewed_source_trees(self):
        for output in (self.video, self.video / "generated", self.sensor, self.sensor.parent,
                       self.idf / "build", self.header.parent / "generated"):
            with self.subTest(output=output):
                with self.assertRaises(ValueError):
                    self.prepare(output)

    def test_exact_replacement_counts(self):
        source = overlay.read_lf(self.video / overlay.VIDEO_SOURCE).decode()
        anchor = '    ESP_RETURN_ON_ERROR(esp_cam_ctlr_stop(common->cam_ctrl_handle), TAG, "failed to stop CAM ctlr");'
        with self.assertRaises(ValueError):
            overlay.instrument_video(source.replace(anchor, anchor + "\n" + anchor))
        sensor = overlay.read_lf(self.sensor / overlay.SENSOR_SOURCE).decode()
        anchor = "    dvp_dma_deinit(*gdma_chan);"
        with self.assertRaises(ValueError):
            overlay.instrument_sensor(sensor.replace(anchor, anchor + "\n" + anchor))
        with self.assertRaises(ValueError):
            overlay.instrument_sensor(sensor.replace(anchor, ""))

    def test_generated_sources_cannot_be_patched_twice(self):
        self.prepare()
        with self.assertRaises(ValueError):
            overlay.instrument_video((self.output / "esp_video_device_common.c").read_text())
        with self.assertRaises(ValueError):
            overlay.instrument_sensor((self.output / "esp_cam_ctlr_dvp_cam.c").read_text())

    def test_preserves_cleanup_callsite_scope_and_mark_budget(self):
        self.prepare()
        video = (self.output / "esp_video_device_common.c").read_text()
        sensor = (self.output / "esp_cam_ctlr_dvp_cam.c").read_text()
        self.assertEqual(video.count("rodak_camera_teardown_record("), 8)
        self.assertEqual(sensor.count("rodak_camera_teardown_record("), 12)
        self.assertEqual(sensor.count("dvp_dma_deinit(*gdma_chan, false)"), 1)
        self.assertEqual(sensor.count("dvp_dma_deinit(ctlr->dma_chan, false)"), 1)
        self.assertEqual(sensor.count("dvp_dma_deinit(ctlr->dma_chan, true)"), 1)

    def test_production_cmake_replaces_exactly_one_source_per_component(self):
        for scenario in ("valid", "missing_video", "duplicate_video", "missing_sensor", "duplicate_sensor"):
            with self.subTest(scenario=scenario):
                result = subprocess.run([
                    "cmake", "-S", str(ROOT / "tests/camera_teardown_patch/cmake_fixture"),
                    "-B", str(self.root / ("cmake-" + scenario)),
                    "-DRODAKOS_ROOT=" + ROOT.as_posix(), "-DRODAKOS_IDF_PATH=" + IDF_PATH.as_posix(),
                    "-DPython3_EXECUTABLE=" + sys.executable,
                    "-DFIXTURE_CASE=" + scenario], capture_output=True, text=True, timeout=20)
                output = result.stdout + result.stderr
                if scenario == "valid":
                    self.assertEqual(result.returncode, 0, output)
                else:
                    self.assertNotEqual(result.returncode, 0, output)
                    self.assertIn("Expected exactly one reviewed camera source", output)

    def test_only_top_level_manifest_hash_may_change(self):
        original = YAML(typ="safe").load(self.lock.read_text())
        original["manifest_hash"] = "different-platform-manifest"
        # JSON is also YAML; reserialization/key ordering must not affect the graph.
        self.lock.write_text(json.dumps(original, sort_keys=True, indent=2))
        self.assertTrue(self.prepare())
        original.pop("manifest_hash")
        self.lock.write_text(json.dumps(original, sort_keys=True))
        self.assertFalse(self.prepare())

    def test_dependency_graph_drift_is_rejected(self):
        original = YAML(typ="safe").load(self.lock.read_text())
        def dependency(current):
            return current["dependencies"]["espressif/esp_cam_sensor"]
        mutations = {
            "version": lambda current: dependency(current).update(version="2.3.1"),
            "package_hash": lambda current: dependency(current).update(component_hash="0" * 64),
            "source": lambda current: dependency(current)["source"].update(type="git"),
            "registry": lambda current: dependency(current)["source"].update(registry_url="https://example.invalid"),
            "nested_manifest_hash": lambda current: dependency(current).update(manifest_hash="unreviewed"),
            "dependency_edge": lambda current: dependency(current)["dependencies"][0].update(require="public"),
            "direct_dependencies": lambda current: current["direct_dependencies"].pop(),
            "target": lambda current: current.update(target="esp32p4"),
            "lock_format_version": lambda current: current.update(version="4.0.0"),
            "extra_top_level": lambda current: current.update(unreviewed=True),
            "removed_component": lambda current: current["dependencies"].pop("espressif/mqtt"),
        }
        self.prepare()
        expected = {path: path.read_bytes() for path in self.output.iterdir()}
        for name, mutate in mutations.items():
            with self.subTest(change=name):
                changed = copy.deepcopy(original)
                changed["manifest_hash"] = "different-platform-manifest"
                mutate(changed)
                self.lock.write_text(json.dumps(changed))
                with self.assertRaisesRegex(ValueError, "dependency graph differs"):
                    self.prepare()
                self.assertEqual(expected, {path: path.read_bytes() for path in self.output.iterdir()})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--idf-path", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    IDF_PATH = args.idf_path
    unittest.main(argv=[__file__, *remaining])
