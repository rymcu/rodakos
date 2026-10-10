"""Build explicit mutations outside the checkout; success means each was detected."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--idf-path", default=os.environ.get("RODAKOS_IDF_PATH"),
                        help="ESP-IDF 6.0.2 tree for suites that compile pinned IDF sources")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    adapter = (repo / "main/phone_os/screen_jpeg_allocation.cc").read_text()
    service = (repo / "main/phone_os/display_service.cc").read_text()
    camera = (repo / "main/phone_os/camera_service.cc").read_text()
    variants = {
        "global-policy": (adapter, "thread_local bool g_screen_jpeg_scope_active", "bool g_screen_jpeg_scope_active"),
        "scope-bypass": (adapter, "if (!g_screen_jpeg_scope_active)", "if (true)"),
        "internal-fallback": (adapter, "return heap_caps_calloc(1, size, kExternalCaps);",
            "if (auto* p = heap_caps_calloc(1, size, kExternalCaps)) return p;\n    return heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);"),
        "missing-service-scope": (service, "ScreenJpegAllocationScope allocation_scope;", "// Deliberately missing scope"),
        "missing-camera-scope": (camera, "ScreenJpegAllocationScope allocation_scope;", "// Deliberately missing scope"),
    }
    # (suite, CMake source option, test binary) for mutations outside the adapter.
    targets = {
        "missing-service-scope": ("display_service", "DISPLAY_SERVICE_SOURCE", "rodakos_display_service_tests"),
        "missing-camera-scope": ("camera_capture", "CAMERA_SERVICE_SOURCE", "rodakos_camera_capture_tests"),
    }
    results = {}
    for name, (source, before, after) in variants.items():
        if before not in source:
            raise RuntimeError(f"mutation source drift: {name}")
        mutated = output / (name + ".cc")
        mutated.write_text(source.replace(before, after))
        suite, option, binary = targets.get(
            name, ("screen_jpeg_allocator", "ADAPTER_SOURCE", "rodakos_screen_jpeg_allocator_tests"))
        build = output / name
        configure = ["cmake", "-S", str(repo / "tests" / suite), "-B", str(build), "-G", "Ninja",
                     "-DCMAKE_BUILD_TYPE=Debug", f"-D{option}={mutated}"]
        if suite == "camera_capture":
            if not args.idf_path:
                raise RuntimeError("missing-camera-scope requires --idf-path or RODAKOS_IDF_PATH")
            configure.append(f"-DRODAKOS_IDF_PATH={args.idf_path}")
        subprocess.run(configure, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["cmake", "--build", str(build), "-j2"], check=True, stdout=subprocess.DEVNULL)
        result = subprocess.run([str(build / binary)], capture_output=True, text=True, timeout=30)
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
