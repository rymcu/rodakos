"""Require credential lifecycle cases to reject isolated complete service mutations."""
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
    pending = "const bool pending = client_ != nullptr && auth_refresh_generation_ == client_generation_;"
    variants = [
        ("discard-new-client-rejection", [(pending, "const bool pending = false;")],
         "new-client rejection received before SDK start returns", "check failed: WaitUntil([&]()"),
        ("forget-same-authority-ledger", [
            ("    recent_published_events_ = {};\n    if (auth_refresh_generation_ == retired_generation)",
             "    recent_published_events_ = {};\n    ResetEffectAuthorityLocked();\n"
             "    if (auth_refresh_generation_ == retired_generation)")],
         "discards the SDK outbox and old queued receipts on the wire",
         'check failed: Get(Get(replay.get(), "receipt"), "configurationRevision")->valueint == 1'),
        ("destroy-after-unconfirmed-stop", [
            ("    if (stop_err != ESP_OK) {\n", "    if (stop_err != ESP_OK) {\n"
             "        esp_mqtt_client_destroy(client->handle);\n")],
         "quarantines a stop failure until the old SDK has really exited",
         'check failed: event.action != "unsafe-destroy-attempt"'),
        ("carry-retired-rejection", [
            ("    if (auth_refresh_generation_ == retired_generation) {\n"
             "        auth_refresh_generation_ = 0;",
             "    if (auth_refresh_generation_ == retired_generation) {\n"
             "        auth_refresh_generation_ = retired_generation;"),
            (pending, "const bool pending = client_ != nullptr && auth_refresh_generation_ != 0;")],
         "coalesces old HTTP-time rejection and preserves a new-client rejection",
         "check failed: RefreshCalls() == 2u"),
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
            ["cmake", "--build", str(build), "--target", "rodakos_mqtt_credential_replacement_tests", "-j", "2"],
        ]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=150)
            (directory / f"build-{index}.log").write_text(result.stdout + result.stderr,
                                                        encoding="utf-8", newline="\n")
            if result.returncode != 0:
                raise RuntimeError("Negative build failed, not a detected regression: " + name)
        result = subprocess.run([str(build / "rodakos_mqtt_credential_replacement_tests"),
                                 "--filter", test_filter],
                                capture_output=True, text=True, timeout=20)
        output = result.stdout + result.stderr
        (directory / "result.log").write_text(output, encoding="utf-8", newline="\n")
        if (result.returncode != 1 or "1 tests, 1 failures" not in output
                or expected_failure not in output
                or re.search(r"AddressSanitizer|LeakSanitizer|runtime error:", output)):
            raise RuntimeError(f"Negative did not fail through its intended assertion: {name}\n{output}")
        compiled = json.loads((build / "production-sources.json").read_text())
        mutated_sha = hashlib.sha256(translation_unit.read_bytes()).hexdigest()
        if compiled["unified_mqtt_service.cc"] != mutated_sha:
            raise RuntimeError("Compiled negative source does not match: " + name)
        report["negative_controls"].append({"variant": name, "detected": True,
            "source_sha256": mutated_sha, "test_filter": test_filter,
            "expected_failure": expected_failure, "returncode": result.returncode})
        print("Detected " + name, flush=True)
    if SOURCE.read_bytes() != original:
        raise RuntimeError("Production source changed during negative controls")
    (args.output / "negative-results.json").write_text(json.dumps(report, indent=2) + "\n",
                                                     encoding="utf-8", newline="\n")
    print(f"{len(variants)} negative controls detected; production source unchanged")


if __name__ == "__main__":
    main()
