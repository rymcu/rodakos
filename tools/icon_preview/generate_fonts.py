#!/usr/bin/env python3
"""Generate candidate 30px desktop icon fonts for the icon preview.

Downloads the pinned npm packages listed in iconsets.json, instances variable fonts with
fontTools or builds a font from Iconify JSON icons (iconify_font.py), and converts each
candidate set to an LVGL font with lv_font_conv. It also writes generated/glyphs.json, the
resolved codepoint per set and app, for the preview program. All downloads and outputs stay
under --work-dir; nothing is written into the repository.
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

LV_FONT_CONV = "lv_font_conv@1.5.3"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True,
                        help="Scratch directory for downloads and generated fonts")
    parser.add_argument("--config", type=Path,
                        default=Path(__file__).with_name("iconsets.json"))
    return parser.parse_args()


def run(command: list[str], cwd: Path) -> None:
    # npm and npx are .cmd shims on Windows, which subprocess only resolves through the shell.
    resolved = shutil.which(command[0])
    if resolved is None:
        sys.exit(f"{command[0]} is not on PATH")
    subprocess.run([resolved, *command[1:]], cwd=cwd, check=True)


def fetch_package(name: str, package: dict, downloads: Path) -> Path:
    target = downloads / name
    if target.is_dir():
        return target
    archive_dir = downloads / f"{name}-archive"
    archive_dir.mkdir(parents=True, exist_ok=True)
    run(["npm", "pack", f"{package['npm']}@{package['version']}", "--silent"], archive_dir)
    archive = next(archive_dir.glob("*.tgz"))
    target.mkdir()
    with tarfile.open(archive) as tar:
        tar.extractall(target, filter="data")
    return target / "package"


def static_font(source: dict, package_dir: Path, fonts: Path, key: str) -> Path:
    font_path = package_dir / source["file"]
    axes = source.get("axes")
    if not axes:
        return font_path
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    instance = instantiateVariableFont(TTFont(font_path), axes, inplace=False)
    instance.flavor = None
    output = fonts / f"{key}.ttf"
    instance.save(output)
    return output


def production_codepoints(glyphs: dict[str, str]) -> dict[str, str]:
    """Maps app -> ICON_* name to app -> hex codepoint using the generated rodakos_icons.h."""
    header = (Path(__file__).resolve().parents[2] / "components" / "rodakos_fonts" / "include"
              / "rodakos_icons.h").read_text(encoding="utf-8")
    codepoints = {}
    for name, literal in re.findall(r'#define ICON_(\w+)\s+"([^"]+)"', header):
        raw = bytes(int(value, 16) for value in re.findall(r"\\x([0-9a-f]{2})", literal))
        codepoints[name] = f"{ord(raw.decode('utf-8')):x}"
    return {app: codepoints[name] for app, name in glyphs.items()}


def main() -> None:
    args = parse_args()
    config = json.loads(args.config.read_text(encoding="utf-8"))
    work = args.work_dir.resolve()
    downloads = work / "downloads"
    fonts = work / "fonts"
    generated = work / "generated"
    for directory in (downloads, fonts, generated):
        directory.mkdir(parents=True, exist_ok=True)

    package_dirs = {}
    for name, package in config["packages"].items():
        package_dir = fetch_package(name, package, downloads)
        package_dirs[name] = package_dir if (package_dir / "package.json").exists() \
            else package_dir / "package"

    # Codepoints per set and app, as hex strings; the preview program reads this file.
    resolved = {}
    for key, icon_set in config["sets"].items():
        source = icon_set["source"]
        if source == "production":
            resolved[key] = production_codepoints(icon_set["glyphs"])
            continue
        package_dir = package_dirs[source["package"]]
        if "iconify" in source:
            # Isolated mode (-I) leaves the script directory off sys.path.
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            import iconify_font

            data = iconify_font.load_icon_set(package_dir / "icons.json")
            font_path = fonts / f"{key}.ttf"
            names = icon_set["glyphs"]
            unique = list(dict.fromkeys(names.values()))
            codepoints = iconify_font.build_font([(data, name) for name in unique], font_path,
                                                 source.get("stroke_width"))
            resolved[key] = {app: f"{codepoints[f'{data['prefix']}:{name}']:x}"
                             for app, name in names.items()}
        else:
            font_path = static_font(source, package_dir, fonts, key)
            resolved[key] = icon_set["glyphs"]
        codepoints = sorted(set(resolved[key].values()))
        font_name = f"icon_{key}_{config['size']}"
        run(["npx", "--yes", LV_FONT_CONV, "--no-compress", "--no-prefilter",
             "--force-fast-kern-format", "--font", str(font_path),
             "-r", ",".join(f"0x{codepoint}" for codepoint in codepoints),
             "--format", "lvgl", "--lv-include", "lvgl.h",
             "--bpp", str(config["bpp"]), "--size", str(config["size"]),
             "--lv-font-name", font_name, "-o", str(generated / f"{font_name}.c")], work)
        print(f"{key}: {len(codepoints)} glyphs -> {font_name}.c")
    (generated / "glyphs.json").write_text(
        json.dumps({"sets": {key: {"glyphs": glyphs} for key, glyphs in resolved.items()}},
                   indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
