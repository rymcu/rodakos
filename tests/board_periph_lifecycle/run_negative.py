"""Verify that the reviewed legacy implementation fails the retry contract."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    cases = {
        "deinit_failure_retry": "retry invokes the real deinit callback",
        "missing_callback_retry": "callback retry invokes real deinit",
    }
    results = []
    for scenario, marker in cases.items():
        result = subprocess.run(
            [str(args.executable), scenario],
            capture_output=True,
            text=True,
            check=False,
            timeout=5,
        )
        output = result.stdout + result.stderr
        results.append({"scenario": scenario, "returncode": result.returncode, "output": output})
        if result.returncode != 1 or any(marker in output for marker in
                ("AddressSanitizer", "LeakSanitizer", "runtime error:")):
            raise SystemExit(f"legacy control did not fail normally: {scenario}\n{output}")
        if f"FAIL: {marker}" not in output:
            raise SystemExit(f"legacy control missed expected failure: {scenario}\n{output}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"results": results}, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
