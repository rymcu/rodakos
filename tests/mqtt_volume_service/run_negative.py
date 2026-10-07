"""Require MQTT diagnostic regressions to reject isolated full service mutations."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "main/phone_os/unified_mqtt_service.cc"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--c-flags", default="")
    parser.add_argument("--cxx-flags", default="")
    parser.add_argument("--link-flags", default="")
    args = parser.parse_args()
    original = SOURCE.read_bytes()
    source = original.decode().replace("\r\n", "\n")
    reason_expression = 'count_limit_reached ? "count_limit" : "byte_limit"'
    variants = [
        ("merged-inbound-reason", [
            ('drop_reason = "object_alloc_failed";', 'drop_reason = "queue_or_allocation_failed";'),
            ('drop_reason = "queue_send_rejected";', 'drop_reason = "queue_or_allocation_failed";')],
         "object allocation failure never sends", "logs[0].message.find(expected) != std::string::npos"),
        ("allocation-as-queue", [
            ('drop_reason = "object_alloc_failed";', 'drop_reason = "queue_send_rejected";')],
         "object allocation failure never sends", "logs[0].message.find(expected) != std::string::npos"),
        ("reason-from-depth", [
            ('const UBaseType_t queue_depth_sample = uxQueueMessagesWaiting(message_queue_);',
             'const UBaseType_t queue_depth_sample = uxQueueMessagesWaiting(message_queue_);\n'
             '                    if (queue_depth_sample < 8) drop_reason = "object_alloc_failed";')],
         "queue reason survives a real drain", "logs[0].message.find(expected) != std::string::npos"),
        ("success-false-drop", [
            ('                    context.release();',
             '                    context.release();\n'
             '                    ESP_LOGE(TAG, "MQTT message dropped: reason=queue_send_rejected");')],
         "successful admission has no drop log", "DiagnosticLogs().empty()"),
        ("merged-outbound-reason", [(reason_expression, '"count_limit"')],
         "outbound exact byte budget", "logs[0].message.find(expected) != std::string::npos"),
        ("byte-before-count", [(reason_expression,
             '(command_publication_bytes_ >= kMaxCommandPublicationBytes) ? "byte_limit" : "count_limit"')],
         "outbound count reason takes precedence", "logs[0].message.find(expected) != std::string::npos"),
    ]
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"production_sha256": hashlib.sha256(original).hexdigest(), "negative_controls": []}
    for name, replacements, test_filter, expected_failure in variants:
        mutated = source
        for before, after in replacements:
            if mutated.count(before) != 1:
                raise RuntimeError("Reviewed mutation boundary drifted: " + name)
            mutated = mutated.replace(before, after, 1)
        directory = args.output / name
        directory.mkdir(parents=True, exist_ok=True)
        translation_unit = directory / "unified_mqtt_service.cc"
        translation_unit.write_text(mutated, encoding="utf-8", newline="\n")
        build = directory / "build"
        commands = [
            ["cmake", "-S", str(ROOT / "tests/mqtt_volume_service"), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Debug", "-DRODAK_MQTT_SERVICE_SOURCE=" + str(translation_unit.resolve()),
             "-DCMAKE_C_FLAGS=" + args.c_flags, "-DCMAKE_CXX_FLAGS=" + args.cxx_flags,
             "-DCMAKE_EXE_LINKER_FLAGS=" + args.link_flags],
            ["cmake", "--build", str(build), "--target", "rodakos_mqtt_queue_diagnostics_tests", "-j", "2"]
        ]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=150)
            (directory / f"build-{index}.log").write_text(result.stdout + result.stderr,
                                                       encoding="utf-8", newline="\n")
            if result.returncode != 0:
                raise RuntimeError("Negative build failed, not a detected regression: " + name)
        result = subprocess.run([str(build / "rodakos_mqtt_queue_diagnostics_tests"), "--filter", test_filter],
                                capture_output=True, text=True, timeout=20)
        output = result.stdout + result.stderr
        (directory / "result.log").write_text(output, encoding="utf-8", newline="\n")
        if (result.returncode != 1 or "1 tests, 1 failures" not in output
                or "check failed: " + expected_failure not in output
                or re.search(r"AddressSanitizer|LeakSanitizer|runtime error:", output)):
            raise RuntimeError(f"Negative did not fail through its intended assertion: {name}\n{output}")
        compiled = json.loads((build / "production-sources.json").read_text())
        mutated_sha = hashlib.sha256(translation_unit.read_bytes()).hexdigest()
        if compiled["unified_mqtt_service.cc"] != mutated_sha:
            raise RuntimeError("Compiled negative source does not match: " + name)
        report["negative_controls"].append({"variant": name, "detected": True,
            "source_sha256": mutated_sha, "test_filter": test_filter,
            "expected_failure": "check failed: " + expected_failure, "returncode": result.returncode})
        print("Detected " + name, flush=True)
    if SOURCE.read_bytes() != original:
        raise RuntimeError("Production source changed during negative controls")
    (args.output / "negative-results.json").write_text(json.dumps(report, indent=2) + "\n",
                                                    encoding="utf-8", newline="\n")
    print(f"{len(variants)} negative controls detected; production source unchanged")


if __name__ == "__main__":
    main()
