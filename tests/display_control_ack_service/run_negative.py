"""Require real full-TU regressions to reject isolated production-source mutations."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "main/phone_os/webrtc_display_service.cc"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--c-flags", default="")
    parser.add_argument("--cxx-flags", default="")
    parser.add_argument("--link-flags", default="")
    args = parser.parse_args()
    before = SOURCE.read_bytes()
    source = before.decode().replace("\r\n", "\n")
    variants = [
        ("old-preopen-drop", "if (running_ && !stop_requested_) {",
         "if (running_ && channel_open_ && !stop_requested_) {",
         "preopen first JPEG waits", "check failed: video_returned"),
        ("old-clear-retains-capacity", "pending_jpeg_to_release.swap(pending_jpeg_);",
         "pending_jpeg_.clear();", "preopen cancelled handshake returns actual JPEG allocation",
         "check failed: allocation_watches[0].releases.load() == 1u"),
        ("missing-video-final-lease", "!lease || !lease->TryApply([&] { ret = esp_peer_send_data(peer, &frame); })",
         "!([&] { ret = esp_peer_send_data(peer, &frame); return true; })()",
         "JPEG next fragment rejects revoked lease", "check failed: stopped"),
        ("missing-ack-final-lease", "current && lease && lease->TryApply([&] {\n        result = esp_peer_send_data(peer, &response);\n    })",
         "current && lease && ([&] { result = esp_peer_send_data(peer, &response); return true; })()",
         "production peer ACK rejects lease revoked", "check failed: stopped"),
        ("early-ack-lease", "current && lease && lease->TryApply([&] {\n        result = esp_peer_send_data(peer, &response);\n    })",
         "current && rodak_early_lease_admission && ([&] { result = esp_peer_send_data(peer, &response); return true; })()",
         "display ACK revocation during successful JSON encoding", "check failed: host::SentFrames().empty()"),
    ]
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"production_sha256": hashlib.sha256(before).hexdigest(), "negative_controls": []}
    for name, original, replacement, test_filter, failure in variants:
        if source.count(original) != 1:
            raise RuntimeError(f"Reviewed mutation boundary drifted: {name}")
        directory = args.output / name
        directory.mkdir(parents=True, exist_ok=True)
        mutated = directory / "webrtc_display_service.cc"
        mutated_source = source.replace(original, replacement, 1)
        if name == "early-ack-lease":
            boundary = "    cJSON* response_json = cJSON_CreateObject();"
            if mutated_source.count(boundary) != 1:
                raise RuntimeError("Reviewed early ACK admission boundary drifted")
            mutated_source = mutated_source.replace(boundary,
                "    const bool rodak_early_lease_admission = lease && lease->IsActive();\n" + boundary, 1)
        mutated.write_text(mutated_source, encoding="utf-8", newline="\n")
        build = directory / "build"
        commands = [
            ["cmake", "-S", str(ROOT / "tests/display_control_ack_service"), "-B", str(build),
             "-DCMAKE_BUILD_TYPE=Debug", "-DRODAK_DISPLAY_NEGATIVE_SOURCE=" + str(mutated.resolve()),
             "-DCMAKE_C_FLAGS=" + args.c_flags, "-DCMAKE_CXX_FLAGS=" + args.cxx_flags,
             "-DCMAKE_EXE_LINKER_FLAGS=" + args.link_flags],
            ["cmake", "--build", str(build), "-j", "2"]]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            (directory / f"build-{index}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            if result.returncode != 0:
                raise RuntimeError(f"Negative build failed, not a detected regression: {name}")
        executable = build / "rodakos_display_control_ack_service_tests"
        result = subprocess.run([str(executable), "--filter", test_filter],
                                capture_output=True, text=True, timeout=15)
        output = result.stdout + result.stderr
        (directory / "result.log").write_text(output, encoding="utf-8")
        if (result.returncode != 1 or "1 tests, 1 failures" not in output or failure not in output
                or re.search(r"AddressSanitizer|LeakSanitizer|runtime error:", output)):
            raise RuntimeError(f"Negative control did not fail through its intended assertion: {name}\n{output}")
        report["negative_controls"].append({"variant": name, "detected": True,
            "source_sha256": hashlib.sha256(mutated.read_bytes()).hexdigest(),
            "test_filter": test_filter, "expected_failure": failure, "returncode": result.returncode})
        print(f"Detected {name}", flush=True)
    if SOURCE.read_bytes() != before:
        raise RuntimeError("Production source changed during negative controls")
    (args.output / "negative-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(variants)} negative controls detected; production source unchanged")


if __name__ == "__main__":
    main()
