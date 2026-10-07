"""Require real MQTT/cloud integration to reject incomplete credential fences."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "main/phone_os/device_cloud_config.cc"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--c-flags", default="")
    parser.add_argument("--cxx-flags", default="")
    parser.add_argument("--link-flags", default="")
    args = parser.parse_args()
    original = SOURCE.read_bytes()
    source = original.decode().replace("\r\n", "\n")
    boundary = "if (mqtt_snapshot(snapshot) != mqtt_snapshot(*current)) return false;"
    start = source.index("bool DeviceCloudConfigService::ApplyIfMqttConfigCurrent(")
    end = source.index("\nProvisioningUrlSaveResult DeviceCloudConfigService::SaveSerialProvisioning(", start)
    variants = [
        ("generation-only-fence", source.replace(boundary,
            "if (snapshot.cloud_generation != current->cloud_generation) return false;", 1),
         "real same generation rotation after SDK initialization",
         'clients.back().credential == "latest-at-attach"'),
        ("bypassed-fence", source[:start] +
         "bool DeviceCloudConfigService::ApplyIfMqttConfigCurrent(\n"
         "    const DeviceCloudConfig&, const std::function<bool()>& apply) { return apply(); }\n" +
         source[end:], "real USB provisioning invalidates the loaded generation", "client.destroyed"),
    ]
    if source.count(boundary) != 1:
        raise RuntimeError("Reviewed fence mutation boundary drifted")
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"production_sha256": hashlib.sha256(original).hexdigest(), "negative_controls": []}
    for name, mutated, test_filter, failure in variants:
        directory = args.output / name
        directory.mkdir(parents=True, exist_ok=True)
        translation_unit = directory / "device_cloud_config.cc"
        translation_unit.write_text(mutated, encoding="utf-8", newline="\n")
        build = directory / "build"
        commands = [
            ["cmake", "-S", str(ROOT / "tests/mqtt_cloud_integration"), "-B", str(build), "-G", "Ninja",
             "-DCMAKE_BUILD_TYPE=Debug", "-DRODAK_CLOUD_SOURCE=" + str(translation_unit.resolve()),
             "-DCMAKE_C_FLAGS=" + args.c_flags, "-DCMAKE_CXX_FLAGS=" + args.cxx_flags,
             "-DCMAKE_EXE_LINKER_FLAGS=" + args.link_flags],
            ["cmake", "--build", str(build), "--target", "mqtt_cloud_integration_tests", "-j", "2"],
        ]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=150)
            (directory / f"build-{index}.log").write_text(result.stdout + result.stderr,
                                                       encoding="utf-8", newline="\n")
            if result.returncode != 0:
                raise RuntimeError("Negative build failed, not a detected regression: " + name)
        result = subprocess.run([str(build / "mqtt_cloud_integration_tests"), "--filter", test_filter],
                                capture_output=True, text=True, timeout=20)
        output = result.stdout + result.stderr
        (directory / "result.log").write_text(output, encoding="utf-8", newline="\n")
        if (result.returncode != 1 or "1 tests, 1 failures" not in output
                or "check failed: " + failure not in output
                or re.search(r"AddressSanitizer|LeakSanitizer|runtime error:", output)):
            raise RuntimeError(f"Negative missed its intended assertion: {name}\n{output}")
        compiled = json.loads((build / "production-sources.json").read_text())
        mutated_sha = hashlib.sha256(translation_unit.read_bytes()).hexdigest()
        if compiled["device_cloud_config.cc"] != mutated_sha:
            raise RuntimeError("Compiled negative source does not match: " + name)
        report["negative_controls"].append({"variant": name, "detected": True,
            "source_sha256": mutated_sha, "test_filter": test_filter,
            "expected_failure": "check failed: " + failure, "returncode": result.returncode})
        print("Detected " + name, flush=True)
    if SOURCE.read_bytes() != original:
        raise RuntimeError("Production cloud source changed during negative controls")
    (args.output / "negative-results.json").write_text(json.dumps(report, indent=2) + "\n",
                                                    encoding="utf-8", newline="\n")
    print(f"{len(variants)} negative controls detected; production source unchanged")


if __name__ == "__main__":
    main()
