"""Offline regressions for firmware CI input validation."""

import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import yaml


SCRIPT = Path(__file__).with_name("firmware_ci.py")
SPEC = importlib.util.spec_from_file_location("firmware_ci", SCRIPT)
firmware_ci = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(firmware_ci)


class RegistrySourceTests(unittest.TestCase):
    def test_both_reviewed_registry_spellings_are_accepted(self):
        for url in ("https://components.espressif.com", "https://components.espressif.com/"):
            with self.subTest(url=url):
                firmware_ci.validate_registry_source("fixture/component", {"registry_url": url})

    def test_every_service_entry_in_actual_lock_is_accepted_without_rewriting(self):
        path = firmware_ci.ROOT / "dependencies.lock"
        original = path.read_bytes()
        lock = yaml.safe_load(original)
        count = 0
        for name, entry in lock["dependencies"].items():
            if entry["source"]["type"] == "service":
                with self.subTest(component=name):
                    firmware_ci.validate_registry_source(name, entry["source"])
                count += 1
        self.assertGreater(count, 0)
        self.assertEqual(path.read_bytes(), original)

    def test_unreviewed_or_malformed_registry_values_are_rejected(self):
        values = (
            "http://components.espressif.com/",
            "https://components.espressif.com.evil.example/",
            "https://mirror.example/",
            "https://components.espressif.com:443/",
            "https://user@components.espressif.com/",
            "https://components.espressif.com@evil.example/",
            "https://components.espressif.com/api/",
            "https://components.espressif.com//",
            "https://components.espressif.com/../",
            "https://components.espressif.com/?query=1",
            "https://components.espressif.com/#fragment",
            "https://components.espressif.com\\",
            "https://COMPONENTS.ESPRESSIF.COM/",
            " https://components.espressif.com/",
            "https://components.espressif.com/\n",
            "", None, [], {}
        )
        for value in values:
            with self.subTest(value=value):
                with self.assertRaisesRegex(RuntimeError, "Unreviewed component Registry"):
                    firmware_ci.validate_registry_source("fixture/component", {"registry_url": value})
        for source in ({}, None):
            with self.subTest(source=source):
                with self.assertRaisesRegex(RuntimeError, "Unreviewed component Registry"):
                    firmware_ci.validate_registry_source("fixture/component", source)


class LockedGraphTests(unittest.TestCase):
    def setUp(self):
        self.lock = yaml.safe_load((firmware_ci.ROOT / "dependencies.lock").read_bytes())

    def test_actual_manager_solver_rejects_new_usb_version_with_all_locked_constraints(self):
        from idf_component_manager.version_solver.helper import PackageSource
        from idf_component_manager.version_solver.mixology.package import Package
        from idf_component_tools import ComponentManagerSettings
        from idf_component_tools.manifest import SolvedComponent
        from idf_component_tools.utils import HashedComponentVersion

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "constraints.txt"
            path.write_text(firmware_ci.locked_component_constraints(self.lock), encoding="utf-8")
            with patch.dict(os.environ, {"IDF_COMPONENT_CONSTRAINT_FILES": str(path),
                                         "IDF_COMPONENT_CONSTRAINTS": ""}):
                constraints = ComponentManagerSettings().constraints
                services = {name: entry for name, entry in self.lock["dependencies"].items()
                            if entry["source"]["type"] == "service"}
                self.assertEqual(set(constraints), set(services))
                for name, entry in services.items():
                    self.assertEqual(constraints[name].min, HashedComponentVersion(entry["version"]))
                    self.assertEqual(constraints[name].max, HashedComponentVersion(entry["version"]))
                component = SolvedComponent(name="espressif/usb", **services["espressif/usb"])
                package = Package(component.name, component.source)
                solver = PackageSource()
                solver.add(package, "1.5.0")
                solver.add(package, "1.6.0")
                self.assertEqual(solver.versions_for(package), [HashedComponentVersion("1.5.0")])

    def test_only_manifest_hash_may_change(self):
        current = copy.deepcopy(self.lock)
        current["manifest_hash"] = "different-platform-manifest"
        with tempfile.TemporaryDirectory() as directory:
            firmware_ci.verify_dependency_graph(self.lock, current, Path(directory))
            self.assertEqual(json.loads((Path(directory) / "resolved-lock.json").read_text()), current)

    def test_graph_drift_is_rejected_with_original_and_changed_evidence(self):
        changes = (
            lambda lock: lock["dependencies"]["espressif/usb"].update(version="1.6.0"),
            lambda lock: lock["dependencies"]["espressif/usb"].update(component_hash="changed"),
            lambda lock: lock["dependencies"]["espressif/usb"]["source"].update(registry_url="https://mirror.example/"),
            lambda lock: lock["dependencies"]["cmake_utilities"]["source"].update(path="components/other"),
            lambda lock: lock["dependencies"].pop("cmake_utilities"),
            lambda lock: lock["dependencies"]["espressif/usb"].update(dependencies=[]),
            lambda lock: lock["direct_dependencies"].pop(),
            lambda lock: lock.update(target="esp32"),
        )
        for index, change in enumerate(changes):
            with self.subTest(change=index), tempfile.TemporaryDirectory() as directory:
                current = copy.deepcopy(self.lock)
                change(current)
                output = Path(directory)
                with self.assertRaisesRegex(RuntimeError, "Dependency graph drifted"), patch("builtins.print"):
                    firmware_ci.verify_dependency_graph(self.lock, current, output)
                self.assertEqual(json.loads((output / "source-lock.json").read_text()), self.lock)
                self.assertEqual(json.loads((output / "resolved-lock.json").read_text()), current)
                self.assertTrue((output / "dependency-diff.txt").read_text().startswith("--- source-lock"))


if __name__ == "__main__":
    unittest.main()
