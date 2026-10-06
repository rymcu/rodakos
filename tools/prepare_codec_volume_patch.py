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
    start = b"int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, int volume)\n"
    end = b"int esp_codec_dev_set_vol_handler("
    require(source.count(start) == 1 and source.count(end) == 1,
            "Reviewed codec volume function boundaries not found")
    first = source.index(start)
    last = source.index(end, first)
    replacement = read_lf(PATCH_DIR / "set_out_vol.c").rstrip(b"\n") + b"\n\n"
    patched = source[:first] + replacement + source[last:]
    if output.exists() and output.read_bytes() == patched:
        return False
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_bytes(patched)
    temporary.replace(output)
    return True


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
