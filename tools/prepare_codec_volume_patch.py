"""Generate a checked esp_codec_dev source overlay without modifying managed dependencies."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


PATCH_DIR = Path(__file__).resolve().parents[1] / "patches" / "esp_codec_dev" / "1.5.7"


def read_lf(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def require_hash(path: Path, expected: str) -> bytes:
    content = read_lf(path)
    actual = hashlib.sha256(content).hexdigest()
    require(actual == expected, f"Unreviewed codec source/metadata: {path} (SHA-256 {actual})")
    return content


def scalar(text: str, key: str, indent: int) -> str:
    matches = re.findall(rf"(?m)^{' ' * indent}{re.escape(key)}: ([^\n]+)$", text)
    require(len(matches) == 1, f"Expected exactly one {key} at indentation {indent}")
    return matches[0].strip().strip("\"'")


def replace_checked(source: bytes, before: bytes, after: bytes, count: int = 1) -> bytes:
    require(source.count(before) == count, "Reviewed codec patch boundary changed")
    return source.replace(before, after)


def replace_function(source: bytes, start: bytes, end: bytes, patch_name: str) -> bytes:
    require(source.count(start) == 1 and source.count(end) == 1,
            f"Reviewed codec function boundary changed: {patch_name}")
    first = source.index(start)
    last = source.index(end, first)
    replacement = read_lf(PATCH_DIR / patch_name).rstrip(b"\n") + b"\n\n"
    return source[:first] + replacement + source[last:]


def patch_i2s(source: bytes) -> bytes:
    source = replace_checked(source, b"#include <stdlib.h>\n",
                             b"#include <stdlib.h>\n#include <stdatomic.h>\n#include <hal/i2s_ll.h>\n")
    anchor = b"static int _i2s_drv_enable(i2s_data_t *i2s_data, bool playback, bool enable)\n"
    source = replace_checked(source, anchor,
                             read_lf(PATCH_DIR / "i2s_format_fault.c") + b"\n" + anchor)
    source = replace_checked(source, b"    if (enable) {\n        ret = i2s_channel_enable(channel);",
                             b"    if (enable && _i2s_format_faulted(i2s_data)) {\n"
                             b"        return ESP_CODEC_DEV_DRV_ERR;\n    }\n"
                             b"    if (enable) {\n        ret = i2s_channel_enable(channel);")
    source = replace_function(source, b"static int set_fs(i2s_data_t *i2s_data, bool playback, bool skip)\n",
                              b"static int check_fs_compatible(", "set_fs.c")
    source = replace_checked(source, b'            ESP_LOGE(TAG, "set_drv_fs failed");',
                             b"            _i2s_latch_format_fault(i2s_data);\n"
                             b'            ESP_LOGE(TAG, "set_drv_fs failed");')
    source = replace_checked(source,
        b"        ret = set_fs(i2s_data, true, true);\n        ret = set_fs(i2s_data, false, true);",
        b"        ret = set_fs(i2s_data, true, true);\n"
        b"        if (ret == ESP_CODEC_DEV_OK) {\n"
        b"            ret = set_fs(i2s_data, false, true);\n        }")
    source = replace_checked(source,
        b"        ret = _i2s_drv_enable(i2s_data, true, enable);\n"
        b"        ret = _i2s_drv_enable(i2s_data, false, enable);",
        b"        ret = _i2s_drv_enable(i2s_data, true, enable);\n"
        b"        if (ret == ESP_CODEC_DEV_OK || !enable) {\n"
        b"            int in_ret = _i2s_drv_enable(i2s_data, false, enable);\n"
        b"            if (ret == ESP_CODEC_DEV_OK) ret = in_ret;\n        }")
    for direction in (b"IN", b"OUT"):
        source = replace_checked(source,
            b"    if (dev_type & ESP_CODEC_DEV_TYPE_" + direction + b") {\n        i2s_data->",
            b"    if (ret == ESP_CODEC_DEV_OK && (dev_type & ESP_CODEC_DEV_TYPE_" + direction +
            b")) {\n        i2s_data->")
    for name in (b"_i2s_data_enable", b"_i2s_data_set_fmt", b"_i2s_data_read", b"_i2s_data_write"):
        start = source.index(b"static int " + name + b"(")
        position = source.index(b"    if (i2s_data->is_open == false)", start)
        check = b"enable && " if name == b"_i2s_data_enable" else b""
        source = source[:position] + b"    if (" + check + b"_i2s_format_faulted(i2s_data)) {\n" + \
            b"        return ESP_CODEC_DEV_DRV_ERR;\n    }\n" + source[position:]
    return source


def write_generated(output: Path, content: bytes) -> bool:
    if output.exists() and output.read_bytes() == content:
        return False
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(content)
    temporary.replace(output)
    return True


def prepare(component_dir: Path, lock_file: Path, project_manifest: Path, output: Path) -> bool:
    component_dir = component_dir.resolve()
    output = output.resolve()
    require(component_dir != output and component_dir not in output.parents,
            "Generated codec source must be outside managed component sources")
    provenance = json.loads((PATCH_DIR / "provenance.json").read_text(encoding="utf-8"))
    name = provenance["component"]
    version = provenance["version"]
    manifest = read_lf(project_manifest).decode("utf-8")
    require(scalar(manifest, name, 2) == version, "Project codec version differs from reviewed pin")

    lock = read_lf(lock_file).decode("utf-8")
    blocks = re.findall(rf"(?m)^  {re.escape(name)}:\n((?:    [^\n]*\n)+)", lock)
    require(len(blocks) == 1, "Codec dependency missing or ambiguous in lock file")
    block = blocks[0]
    for key, expected, indent in (
        ("version", version, 4),
        ("component_hash", provenance["component_hash"], 4),
        ("registry_url", provenance["registry_url"], 6),
        ("type", "service", 6),
    ):
        require(scalar(block, key, indent) == expected, f"Unreviewed locked codec {key}")

    require((component_dir / ".component_hash").read_text(encoding="utf-8").strip()
            == provenance["component_hash"], "Managed codec package hash differs from reviewed release")
    component_manifest = require_hash(component_dir / "idf_component.yml",
                                      provenance["manifest_sha256_lf"]).decode("utf-8")
    for key, expected, indent in (
        ("version", version, 0),
        ("repository", provenance["repository"], 0),
        ("commit_sha", provenance["commit_sha"], 2),
        ("path", provenance["repository_path"], 2),
    ):
        require(scalar(component_manifest, key, indent) == expected,
                f"Unreviewed managed codec {key}")

    source = require_hash(component_dir / "esp_codec_dev.c", provenance["source_sha256_lf"])
    i2s_source = require_hash(component_dir / "platform" / "audio_codec_data_i2s.c",
                              provenance["i2s_source_sha256_lf"])
    patched = replace_function(source,
        b"int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, int volume)\n",
        b"int esp_codec_dev_set_vol_handler(", "set_out_vol.c")
    patched = replace_function(patched,
        b"int esp_codec_dev_open(esp_codec_dev_handle_t handle, esp_codec_dev_sample_info_t *fs)\n",
        b"int esp_codec_dev_read_reg(", "open.c")
    patched_i2s = patch_i2s(i2s_source)
    # Validate both complete upstream sources before writing either build copy.
    changed = write_generated(output, patched)
    return write_generated(output.parent / "platform" / "audio_codec_data_i2s.c", patched_i2s) or changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--component-dir", required=True, type=Path)
    parser.add_argument("--lock-file", required=True, type=Path)
    parser.add_argument("--project-manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        changed = prepare(args.component_dir, args.lock_file, args.project_manifest, args.output)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"Codec volume overlay refused: {error}", file=sys.stderr)
        return 1
    print(f"Codec volume overlay {'generated' if changed else 'verified'}: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
