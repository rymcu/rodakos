"""Compile real peer service mutations and require their specific regression assertions."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--c-flags", default="")
    parser.add_argument("--cxx-flags", default="")
    parser.add_argument("--link-flags", default="")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for kind in ("camera", "display"):
        source_path = ROOT / "main/phone_os" / f"webrtc_{kind}_service.cc"
        original_bytes = source_path.read_bytes()
        source = original_bytes.decode().replace("\r\n", "\n")
        variants = [
            ("implicit-sdk-capacity", "    auto default_cfg = MakeWebRtcPeerDefaultConfig();",
             "    auto default_cfg = MakeWebRtcPeerDefaultConfig();\n    default_cfg.max_candidates = 0;",
             f"{kind} uses explicit SDK candidate capacity", "attempt.defaults.max_candidates == 32u"),
            ("missing-signal-sample", "        peer_resources_.Signal(TAG, type, result);", "        (void)result;",
             f"{kind} samples all resource heaps", "found != logs.end()")]
        for name, before, after, test_filter, expected in variants:
            assert source.count(before) == 1, f"mutation boundary drift: {kind}/{name}"
            directory = args.output / f"{kind}-{name}"
            directory.mkdir(parents=True, exist_ok=True)
            mutated = directory / source_path.name
            mutated.write_text(source.replace(before, after, 1), encoding="utf-8", newline="\n")
            build = directory / "build"
            commands = [
                ["cmake", "-S", str(ROOT / "tests/display_control_ack_service"), "-B", str(build),
                 "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Debug",
                 f"-DRODAK_{kind.upper()}_NEGATIVE_SOURCE=" + str(mutated.resolve()),
                 "-DCMAKE_C_FLAGS=" + args.c_flags, "-DCMAKE_CXX_FLAGS=" + args.cxx_flags,
                 "-DCMAKE_EXE_LINKER_FLAGS=" + args.link_flags],
                ["cmake", "--build", str(build), "-j", "2"]]
            for index, command in enumerate(commands):
                result = subprocess.run(command, capture_output=True, text=True, timeout=90)
                (directory / f"build-{index}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
                assert result.returncode == 0, f"negative build failure is not a detected regression: {kind}/{name}"
            result = subprocess.run([str(build / "rodakos_display_control_ack_service_tests"), "--filter", test_filter],
                                    capture_output=True, text=True, timeout=20)
            output = result.stdout + result.stderr
            (directory / "result.log").write_text(output, encoding="utf-8")
            assert (result.returncode == 1 and "1 tests, 1 failures" in output and
                    "check failed: " + expected in output and
                    not re.search(r"AddressSanitizer|LeakSanitizer|runtime error:", output)), output
            results.append({"service": kind, "variant": name, "detected": True,
                "originalSha256": hashlib.sha256(original_bytes).hexdigest(),
                "mutatedSha256": hashlib.sha256(mutated.read_bytes()).hexdigest(),
                "testFilter": test_filter, "expectedAssertion": expected, "exitCode": result.returncode})
            print(f"Detected {kind}/{name}", flush=True)
        assert source_path.read_bytes() == original_bytes, "production source changed during controls"
    (args.output / "negative-results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"{len(results)} peer config/resource negative controls detected")


if __name__ == "__main__":
    main()
