from __future__ import annotations
import argparse
import json
import subprocess
from pathlib import Path

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    cases = {
        "release_failure_retry": "release failure propagates",
        "invalid_handle_cleanup_retry": "failed cleanup retains ownership",
        "missing_subtype_callback": "missing subtype callback propagates",
    }
    results = []
    for scenario, marker in cases.items():
        result = subprocess.run([str(args.executable), scenario], capture_output=True, text=True,
                                check=False, timeout=5)
        output = result.stdout + result.stderr
        results.append({"scenario": scenario, "returncode": result.returncode, "output": output})
        if (result.returncode != 1 or f"FAIL: {marker}" not in output or
                any(error in output for error in ("AddressSanitizer", "LeakSanitizer", "runtime error:"))):
            raise SystemExit(f"legacy control did not detect {scenario}:\n{output}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"results": results}, indent=2) + "\n", encoding="utf-8")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
