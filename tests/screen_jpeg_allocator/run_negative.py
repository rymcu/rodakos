"""Build explicit mutations outside the checkout; success means each was detected."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    adapter = (repo / "main/phone_os/screen_jpeg_allocation.cc").read_text()
    service = (repo / "main/phone_os/display_service.cc").read_text()
    variants = {
        "global-policy": (adapter, "thread_local bool g_screen_jpeg_scope_active", "bool g_screen_jpeg_scope_active"),
        "scope-bypass": (adapter, "if (!g_screen_jpeg_scope_active)", "if (true)"),
        "internal-fallback": (adapter, "return heap_caps_calloc(1, size, kExternalCaps);",
            "if (auto* p = heap_caps_calloc(1, size, kExternalCaps)) return p;\n    return heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);"),
        "missing-service-scope": (service, "ScreenJpegAllocationScope allocation_scope;", "// Deliberately missing scope"),
    }
    results = {}
    for name, (source, before, after) in variants.items():
        if before not in source:
            raise RuntimeError(f"mutation source drift: {name}")
        mutated = output / (name + ".cc")
        mutated.write_text(source.replace(before, after))
        is_service = name == "missing-service-scope"
        suite = "display_service" if is_service else "screen_jpeg_allocator"
        option = "DISPLAY_SERVICE_SOURCE" if is_service else "ADAPTER_SOURCE"
        build = output / name
        subprocess.run(["cmake", "-S", str(repo / "tests" / suite), "-B", str(build), "-G", "Ninja",
                        "-DCMAKE_BUILD_TYPE=Debug", f"-D{option}={mutated}"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["cmake", "--build", str(build), "-j2"], check=True, stdout=subprocess.DEVNULL)
        result = subprocess.run([str(build / ("rodakos_" + suite + "_tests"))], capture_output=True, text=True, timeout=30)
        (output / (name + ".log")).write_text(result.stdout + result.stderr)
        # A crash alone is not a valid negative: require the test harness's
        # explicit failure evidence and normal nonzero failure status.
        combined = result.stdout + result.stderr
        detected = result.returncode == 1 and "[FAIL]" in combined
        results[name] = {"exit_code": result.returncode, "detected": detected,
                         "summary": result.stdout.splitlines()[-1] if result.stdout else ""}
    (output / "result.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))
    return 0 if all(item["detected"] for item in results.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
