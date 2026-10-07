"""Compile real fixture headers with equal metadata and both include orders."""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = "c++"
EVIDENCE = None
HEADERS = (
    "tests/mqtt_volume_service/fakes/esp_event.h",
    "tests/mqtt_volume_service/fakes/host_sdk.h",
    "tests/server_trust/fakes/esp_http_client.h",
    "tests/server_trust/fakes/host_sdk.h",
)


class HeaderIsolationTests(unittest.TestCase):
    def compile_headers(self, http_first=False, old_wrapper=False):
        with tempfile.TemporaryDirectory(prefix="rodak-mqtt-cloud-header-") as temporary:
            directory = Path(temporary)
            for relative in HEADERS:
                target = directory / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / relative, target)
            mqtt = directory / HEADERS[0]
            http = directory / HEADERS[2]
            if old_wrapper:
                # Reproduce the old shared forwarding form in this isolated copy.
                shutil.copyfile(mqtt, http)
            for relative in HEADERS:
                os.utime(directory / relative, (1_700_000_000, 1_700_000_000))
            self.assertEqual(mqtt.stat().st_mtime_ns, http.stat().st_mtime_ns)
            self.assertNotEqual(mqtt.stat().st_ino, http.stat().st_ino)
            ordered = (http, mqtt) if http_first else (mqtt, http)
            source = directory / "headers.cc"
            source.write_text("#define RODAK_MQTT_REAL_CLOUD 1\n" +
                "".join(f'#include "{path.relative_to(directory).as_posix()}"\n' for path in ordered) +
                "struct CloudContract {\n"
                "  static void configure(esp_http_client_config_t& http);\n"
                "  static void attach(esp_http_client_handle_t client);\n"
                "};\n", encoding="utf-8")
            environment = {**os.environ, "LC_ALL": "C"}
            base = [COMPILER, "-std=c++20", "-Wall", "-Wextra", "-Werror", source.name]
            commands = {"preprocess": base + ["-E", "-H"], "compile": base + ["-fsyntax-only"]}
            preprocessing = subprocess.run(commands["preprocess"], capture_output=True,
                text=True, timeout=30, env=environment, cwd=directory)
            result = subprocess.run(commands["compile"], capture_output=True,
                text=True, timeout=30, env=environment, cwd=directory)
            record = {
                "include_order": [str(path.relative_to(directory)) for path in ordered],
                "compiler": COMPILER, "returncode": result.returncode,
                "preprocess_returncode": preprocessing.returncode,
                "http_definition_present": "struct esp_http_client_config_t {" in preprocessing.stdout,
                "headers": [{"path": str(path.relative_to(directory)), "size": path.stat().st_size,
                    "mtime_ns": path.stat().st_mtime_ns, "inode": path.stat().st_ino,
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in (mqtt, http)],
                "declaration_errors": [line for line in result.stderr.splitlines() if "error:" in line],
            }
            if old_wrapper and EVIDENCE is not None:
                EVIDENCE.mkdir(parents=True, exist_ok=True)
                record["intent"] = "Identical pragma-once wrappers with equal timestamps must reproduce the missing HTTP declarations"
                record["compiler_version"] = subprocess.check_output(
                    [COMPILER, "--version"], text=True, env=environment).splitlines()[0]
                record["commands"] = commands
                record["working_directory"] = "inputs"
                record["environment"] = {"LC_ALL": "C"}
                record["input_files"] = []
                for relative in (*HEADERS, source.name):
                    original = directory / relative
                    saved = EVIDENCE / "inputs" / relative
                    saved.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(original, saved)
                    record["input_files"].append({"path": relative,
                        "sha256": hashlib.sha256(original.read_bytes()).hexdigest(),
                        "mtime_ns": original.stat().st_mtime_ns})
                preprocessed_bytes = preprocessing.stdout.encode("utf-8")
                record["preprocessed_sha256"] = hashlib.sha256(preprocessed_bytes).hexdigest()
                (EVIDENCE / "preprocessed.ii.gz").write_bytes(gzip.compress(preprocessed_bytes, mtime=0))
                (EVIDENCE / "compiler.log").write_text(result.stdout + result.stderr, encoding="utf-8")
                (EVIDENCE / "include-trace.log").write_text(preprocessing.stderr, encoding="utf-8")
                (EVIDENCE / "result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
            self.assertEqual(preprocessing.returncode, 0)
            return result, record

    def test_mqtt_first_keeps_cloud_http_declarations(self):
        result, record = self.compile_headers()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(record["http_definition_present"])

    def test_http_first_keeps_cloud_http_declarations(self):
        result, record = self.compile_headers(http_first=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(record["http_definition_present"])

    def test_identical_old_wrappers_reproduce_the_exact_failure(self):
        result, record = self.compile_headers(old_wrapper=True)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(record["http_definition_present"])
        self.assertEqual(record["headers"][0]["sha256"], record["headers"][1]["sha256"])
        for name in ("esp_http_client_config_t", "esp_http_client_handle_t"):
            self.assertRegex(result.stderr, re.escape(name) + r"' has not been declared")
        self.assertEqual(len(record["declaration_errors"]), 2)
        self.assertNotRegex(result.stderr, r"fatal error:|AddressSanitizer|LeakSanitizer|runtime error:")
        if EVIDENCE is not None:
            record["detected"] = True
            record["expected_missing_declarations"] = ["esp_http_client_config_t", "esp_http_client_handle_t"]
            (EVIDENCE / "result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--negative-evidence", type=Path)
    args, test_args = parser.parse_known_args()
    COMPILER = args.compiler
    ROOT = args.root.resolve()
    EVIDENCE = args.negative_evidence
    unittest.main(argv=[__file__, *test_args])
