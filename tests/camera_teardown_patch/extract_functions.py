"""Compile the generated, hash-checked production C functions against host fakes."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("camera_overlay", ROOT / "tools/prepare_camera_teardown_patch.py")
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf-path", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    overlay.prepare(ROOT / "managed_components/espressif__esp_video",
                    ROOT / "managed_components/espressif__esp_cam_sensor",
                    ROOT / "dependencies.lock", ROOT / "main/idf_component.yml", args.idf_path,
                    ROOT / "main/phone_os/camera-teardown-diagnostics.h", args.output_dir)
    video = (args.output_dir / "esp_video_device_common.c").read_text()
    sensor = (args.output_dir / "esp_cam_ctlr_dvp_cam.c").read_text()
    functions = '\n\n'.join(overlay.function_text(sensor, name) for name in
                            ("dvp_dma_deinit", "dvp_stop_capturing", "dvp_worker_quiesce", "dvp_cam_ctlr_del"))
    functions += "\n\n" + overlay.function_text(video, "common_video_stop")
    wrappers = """
esp_err_t run_common_stop(struct esp_video *video) { return common_video_stop(video, 0); }
esp_err_t run_dvp_del(dvp_cam_ctlr_t *ctlr) { return dvp_cam_ctlr_del(ctlr); }
esp_err_t run_dma_deinit(gdma_channel_handle_t channel, bool record) { return dvp_dma_deinit(channel, record); }
"""
    overlay.write_if_changed(args.output_dir / "production_functions.c",
        '#include "camera_teardown_fakes.h"\n' + functions + wrappers)
    evidence = {}
    for name, path in (
            ("managed_video", ROOT / "managed_components/espressif__esp_video" / overlay.VIDEO_SOURCE),
            ("managed_sensor", ROOT / "managed_components/espressif__esp_cam_sensor" / overlay.SENSOR_SOURCE),
            ("generated_video", args.output_dir / "esp_video_device_common.c"),
            ("generated_sensor", args.output_dir / "esp_cam_ctlr_dvp_cam.c"),
            ("compiled_functions", args.output_dir / "production_functions.c"),
            ("diagnostics_header", ROOT / "main/phone_os/camera-teardown-diagnostics.h")):
        evidence[name] = {"sha256_lf": hashlib.sha256(overlay.read_lf(path)).hexdigest()}
    overlay.write_if_changed(args.output_dir.parent / "production-sources.json",
                             json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    main()
