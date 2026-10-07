"""Offline regressions for firmware CI input validation."""

import copy
import importlib.util
import json
import os
from pathlib import Path
import shlex
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


class CameraTeardownEvidenceTests(unittest.TestCase):
    SOURCES = (
        "main/phone_os/camera-teardown-diagnostics.cc",
        "main/phone_os/camera_service.cc",
        "build/rodak_patches/camera_teardown/esp_video_device_common.c",
        "build/rodak_patches/camera_teardown/esp_cam_ctlr_dvp_cam.c",
    )
    HASH_INPUTS = (
        "main/phone_os/camera-teardown-diagnostics.cc",
        "main/phone_os/camera-teardown-diagnostics.h",
        "main/phone_os/camera_service.cc",
        "tools/check_camera_teardown_diagnostics.py",
        "tools/check_screen_jpeg_allocator.py",
        "tools/prepare_camera_teardown_patch.py",
        "patches/camera_teardown/2.3.0/provenance.json",
    )
    MANAGED = (
        "managed_components/espressif__esp_video/src/device/esp_video_device_common.c",
        "managed_components/espressif__esp_cam_sensor/src/driver_dvp/esp_cam_ctlr_dvp_cam.c",
    )

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="camera ci fixture ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.build = self.root / "build"
        self.output = self.root / "firmware-ci-results"
        self.output.mkdir()
        for source in set(self.SOURCES + self.HASH_INPUTS):
            path = self.root / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture source: " + source + "\n", encoding="utf-8")
        self.config = self.root / "sdkconfig"
        self.config.write_text('CONFIG_IDF_TARGET="esp32s3"\nCONFIG_IDF_TARGET_ESP32S3=y\n'
                               'CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND=y\n', encoding="utf-8")
        self.elf = self.build / "rodakos.elf"
        self.elf.write_bytes(b"fixture final ELF bytes; strong ELF checker runs separately")
        self.report_path = self.build / "camera-teardown-linked.json"
        self.report = {
            "success": True, "camera_teardown_link_verified": True,
            "elf_sha256": firmware_ci.digest(self.elf),
            "sdkconfig_sha256": firmware_ci.digest(self.config),
            "target": "esp32s3", "idf_version": "6.0.2",
            "storage": {"address": 0x3FC88000, "size": 536},
            "recorder": {"address": 0x40370000, "size": 95},
            "generated_code": {"native_cas_count": 1, "calls": [], "backward_branches": []},
        }
        self.write_report(self.report)
        self.commands = [self.command(source, ["-mno-disable-hardware-atomics"] if index == 0
                                      else ["-mdisable-hardware-atomics"])
                         for index, source in enumerate(self.SOURCES)]
        root_patch = patch.object(firmware_ci, "ROOT", self.root)
        root_patch.start()
        self.addCleanup(root_patch.stop)

    def command(self, source, flags=()):
        return {"file": str(self.root / source), "directory": str(self.build),
                "command": shlex.join(["xtensa-esp32s3-elf-g++", *flags, "-c",
                                       (self.root / source).as_posix()])}

    def write_report(self, value):
        self.report_path.write_text(json.dumps(value), encoding="utf-8")

    def inspect(self, commands=None):
        return firmware_ci.inspect_camera_teardown(
            self.build, self.config, self.commands if commands is None else commands, self.output)

    def test_reviewed_sources_and_hashes_are_exported_without_changing_inputs(self):
        original = {source: (self.root / source).read_bytes() for source in set(self.SOURCES + self.HASH_INPUTS)}
        self.inspect()
        evidence = json.loads((self.output / "camera-teardown-build.json").read_text())
        self.assertEqual(evidence["schemaVersion"], 1)
        self.assertEqual(evidence["elfSha256"], firmware_ci.digest(self.elf))
        self.assertEqual(evidence["sdkconfigSha256"], firmware_ci.digest(self.config))
        self.assertEqual(evidence["linkedReportSha256"], firmware_ci.digest(self.report_path))
        self.assertEqual(evidence["sourceHashes"], {source: firmware_ci.digest(self.root / source)
                                                   for source in self.HASH_INPUTS})
        self.assertEqual(evidence["nativeAtomicTranslationUnit"], self.SOURCES[0])
        selected_path = self.output / "selected-camera-compile-commands.json"
        self.assertEqual(evidence["selectedCompileCommandsSha256"], firmware_ci.digest(selected_path))
        selected = json.loads(selected_path.read_text())
        self.assertEqual({entry["sourceRelative"] for entry in selected["entries"]}, set(self.SOURCES))
        self.assertEqual(len(selected["entries"]), 4)
        self.assertEqual(selected["responseFiles"], {})
        for source in self.SOURCES[2:]:
            self.assertEqual((self.output / "camera-generated" / Path(source).name).read_bytes(), original[source])
        self.assertEqual(original, {source: (self.root / source).read_bytes() for source in original})

    def test_relative_source_paths_are_resolved_from_the_compile_directory(self):
        commands = copy.deepcopy(self.commands)
        for entry in commands:
            entry["file"] = os.path.relpath(entry["file"], self.build)
        self.inspect(commands)

    def test_nested_build_response_files_are_expanded_and_recorded(self):
        inner = self.build / "inner flags.rsp"
        outer = self.build / "outer flags.rsp"
        inner.write_text("-mno-disable-hardware-atomics", encoding="utf-8")
        outer.write_text(shlex.join(["-mdisable-hardware-atomics", "@" + inner.as_posix()]), encoding="utf-8")
        commands = copy.deepcopy(self.commands)
        commands[0] = self.command(self.SOURCES[0], ["@" + outer.as_posix()])
        self.inspect(commands)
        selected = json.loads((self.output / "selected-camera-compile-commands.json").read_text())
        entry = next(item for item in selected["entries"] if item["sourceRelative"] == self.SOURCES[0])
        self.assertIn("-mno-disable-hardware-atomics", entry["expandedArguments"])
        self.assertFalse(any(item.startswith("@") for item in entry["expandedArguments"]))
        for response in (inner, outer):
            recorded = selected["responseFiles"][response.relative_to(self.root).as_posix()]
            self.assertEqual(recorded["sha256"], firmware_ci.digest(response))
            self.assertEqual(recorded["text"], response.read_text())

    def test_each_selected_source_must_be_compiled_exactly_once(self):
        for index, source in enumerate(self.SOURCES):
            for duplicate in (False, True):
                with self.subTest(source=source, duplicate=duplicate):
                    commands = copy.deepcopy(self.commands)
                    if duplicate:
                        commands.append(copy.deepcopy(commands[index]))
                    else:
                        commands.pop(index)
                    with self.assertRaises(RuntimeError):
                        self.inspect(commands)

    def test_original_managed_sources_cannot_be_compiled(self):
        for source in self.MANAGED:
            with self.subTest(source=source), self.assertRaises(RuntimeError):
                self.inspect(self.commands + [self.command(source)])

    def test_diagnostics_need_the_last_effective_native_atomic_flag(self):
        for flags in ([], ["-mdisable-hardware-atomics"],
                      ["-mno-disable-hardware-atomics", "-mdisable-hardware-atomics"]):
            with self.subTest(flags=flags):
                commands = copy.deepcopy(self.commands)
                commands[0] = self.command(self.SOURCES[0], flags)
                with self.assertRaises(RuntimeError):
                    self.inspect(commands)

    def test_other_selected_translation_units_cannot_enable_native_atomics(self):
        for index in range(1, len(self.SOURCES)):
            with self.subTest(source=self.SOURCES[index]):
                commands = copy.deepcopy(self.commands)
                commands[index] = self.command(self.SOURCES[index], ["-mno-disable-hardware-atomics"])
                with self.assertRaises(RuntimeError):
                    self.inspect(commands)

    def test_other_selected_translation_units_must_explicitly_disable_native_atomics(self):
        for index in range(1, len(self.SOURCES)):
            with self.subTest(source=self.SOURCES[index]):
                commands = copy.deepcopy(self.commands)
                commands[index] = self.command(self.SOURCES[index], [])
                with self.assertRaisesRegex(RuntimeError, "global atomic workaround"):
                    self.inspect(commands)

    def test_response_files_cannot_escape_build_or_be_missing(self):
        outside = self.root / "outside.rsp"
        outside.write_text("-mno-disable-hardware-atomics", encoding="utf-8")
        for response in (outside, self.build / "missing.rsp"):
            with self.subTest(response=response):
                commands = copy.deepcopy(self.commands)
                commands[0] = self.command(self.SOURCES[0], ["@" + response.as_posix()])
                with self.assertRaises(RuntimeError):
                    self.inspect(commands)

    def test_response_cycles_oversize_and_excessive_nesting_are_rejected(self):
        cyclic = self.build / "cycle.rsp"
        cyclic.write_text(shlex.join(["@" + cyclic.as_posix()]), encoding="utf-8")
        oversized = self.build / "oversized.rsp"
        oversized.write_text(" " * (64 * 1024 + 1), encoding="utf-8")
        nested = [self.build / f"depth-{index}.rsp" for index in range(6)]
        for index, response in enumerate(nested):
            response.write_text(shlex.join(["@" + nested[index + 1].as_posix()]) if index + 1 < len(nested)
                                else "-mno-disable-hardware-atomics", encoding="utf-8")
        for response in (cyclic, oversized, nested[0]):
            with self.subTest(response=response):
                commands = copy.deepcopy(self.commands)
                commands[0] = self.command(self.SOURCES[0], ["@" + response.as_posix()])
                with self.assertRaises(RuntimeError):
                    self.inspect(commands)

    def test_missing_linked_report_is_rejected(self):
        self.report_path.unlink()
        with self.assertRaises(RuntimeError):
            self.inspect()

    def test_failed_or_mismatched_linked_reports_are_rejected(self):
        for field, value in (("success", False), ("camera_teardown_link_verified", False),
                             ("elf_sha256", "0" * 64), ("sdkconfig_sha256", "0" * 64),
                             ("target", "esp32"), ("idf_version", "6.0.1")):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                report[field] = value
                self.write_report(report)
                with self.assertRaises(RuntimeError):
                    self.inspect()

    def test_report_storage_and_recorder_bounds_are_rejected(self):
        changes = (("storage", "size", 535), ("storage", "address", 0x3FC88001),
                   ("storage", "address", 0x3FC87FFC), ("storage", "address", 0x3FCFFFFC),
                   ("recorder", "address", 0x42000000), ("recorder", "size", 0),
                   ("recorder", "address", 0x403DFFFC))
        for section, field, value in changes:
            with self.subTest(section=section, field=field, value=value):
                report = copy.deepcopy(self.report)
                report[section][field] = value
                self.write_report(report)
                with self.assertRaises(RuntimeError):
                    self.inspect()

    def test_report_requires_one_cas_and_no_calls_or_backward_branches(self):
        for field, value in (("native_cas_count", 0), ("native_cas_count", 2),
                             ("calls", ["unexpected-call"]),
                             ("backward_branches", ["unexpected-loop"])):
            with self.subTest(field=field, value=value):
                report = copy.deepcopy(self.report)
                report["generated_code"][field] = value
                self.write_report(report)
                with self.assertRaises(RuntimeError):
                    self.inspect()

    def test_global_workaround_and_s3_config_must_remain_enabled(self):
        original = self.config.read_text()
        for line in original.splitlines():
            with self.subTest(line=line):
                self.config.write_text(original.replace(line + "\n", ""), encoding="utf-8")
                report = copy.deepcopy(self.report)
                report["sdkconfig_sha256"] = firmware_ci.digest(self.config)
                self.write_report(report)
                with self.assertRaises(RuntimeError):
                    self.inspect()


if __name__ == "__main__":
    unittest.main()
