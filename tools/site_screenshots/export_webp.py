"""Convert rendered 320x240 LVGL PPMs and hardware PNG renders into WebP assets for site/public."""

from pathlib import Path
import argparse
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SCREENSHOTS_DST = ROOT / "site" / "public" / "screenshots"
HARDWARE_DST = ROOT / "site" / "public" / "hardware"

# Map source PPM filename -> output WebP stem in site/public/screenshots/
SCREEN_MAP = {
    "home-dark.ppm": "home-dark",
    "home-light.ppm": "home-light",
    "home-blue.ppm": "home-blue",
    "home-green.ppm": "home-green",
    "home-page2-dark.ppm": "home-page2-dark",
    "home-voice-dark.ppm": "voice-caption-dark",
    "home-voice-light.ppm": "voice-caption-light",
    "calendar-month.ppm": "calendar-dark",
    "calendar-light.ppm": "calendar-light",
    "gyro-tilted.ppm": "gyro-dark",
    "gyro-light.ppm": "gyro-light",
    "smart-overview.ppm": "smart-dark",
    "smart-light-theme.ppm": "smart-light",
    "assistant-ready.ppm": "assistant-dark",
    "assistant-thinking.ppm": "assistant-thinking-dark",
    "recorder-recording.ppm": "recorder-dark",
    "themes-builtin.ppm": "themes-dark",
    "themes-light.ppm": "themes-light",
    "themes-trust.ppm": "themes-trust-dark",
    "wol-list.ppm": "wol-dark",
    "wol-light.ppm": "wol-light",
    "music-playing.ppm": "music-dark",
    "files-root.ppm": "files-dark",
}

HARDWARE_MAP = {
    ROOT / "3d" / "enclosure" / "out" / "renders" / "03_exploded_and_cradle.png": ("enclosure-exploded", 1400),
    ROOT / "3d" / "enclosure" / "out" / "renders" / "01_front_rear_left_views.png": ("enclosure-views", 1400),
    ROOT / "design" / "poster" / "output" / "product-detail" / "prepared" / "device-screen-replaced.png": ("bigsmart-front", 1100),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ppm-dir", type=Path, required=True)
    parser.add_argument("--scale", type=int, default=3)
    args = parser.parse_args()

    SCREENSHOTS_DST.mkdir(parents=True, exist_ok=True)
    HARDWARE_DST.mkdir(parents=True, exist_ok=True)

    exported = 0
    for src_name, dst_stem in SCREEN_MAP.items():
        src = args.ppm_dir / src_name
        if not src.exists():
            print(f"[skip] missing {src_name}")
            continue
        with Image.open(src) as img:
            upscaled = img.convert("RGB").resize(
                (img.width * args.scale, img.height * args.scale),
                Image.Resampling.NEAREST,
            )
            dst = SCREENSHOTS_DST / f"{dst_stem}.webp"
            upscaled.save(dst, format="WEBP", lossless=True, method=4)
            exported += 1
            print(f"[screen] {src_name} -> {dst.name} ({upscaled.width}x{upscaled.height})")

    for src_path, (dst_stem, max_w) in HARDWARE_MAP.items():
        if not src_path.exists():
            print(f"[skip] missing hardware asset {src_path}")
            continue
        with Image.open(src_path) as img:
            mode = "RGBA" if img.mode in ("RGBA", "LA") else "RGB"
            out = img.convert(mode)
            if out.width > max_w:
                new_h = round(out.height * (max_w / out.width))
                out = out.resize((max_w, new_h), Image.Resampling.LANCZOS)
            dst = HARDWARE_DST / f"{dst_stem}.webp"
            out.save(dst, format="WEBP", quality=88, method=5)
            print(f"[hardware] {src_path.name} -> {dst.name} ({out.width}x{out.height})")

    print(f"Exported {exported} LVGL screenshots to {SCREENSHOTS_DST}")


if __name__ == "__main__":
    main()
