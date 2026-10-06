"""Generate a checked WebSocket redirect rejection overlay without editing managed sources."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


PATCH_DIR = Path(__file__).resolve().parents[1] / "patches" / "esp_websocket_client" / "1.8.0"
REDIRECT_START = "#if WS_TRANSPORT_REDIRECT_HEADER_SUPPORT\n            else if (WS_HTTP_REDIRECT(result)) {\n"
REDIRECT_END = '#endif\n            ESP_LOGD(TAG, "Transport connected to %s://%s:%d",'


def read_lf(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def require_hash(path: Path, expected: str) -> str:
    content = read_lf(path)
    actual = hashlib.sha256(content).hexdigest()
    require(actual == expected, f"Unreviewed WebSocket source/metadata: {path} (SHA-256 {actual})")
    return content.decode("utf-8")


def scalar(text: str, key: str, indent: int) -> str:
    matches = re.findall(rf"(?m)^{' ' * indent}{re.escape(key)}: ([^\n]+)$", text)
    require(len(matches) == 1, f"Expected exactly one {key} at indentation {indent}")
    return matches[0].strip().strip("\"'")


def write_if_changed(path: Path, text: str) -> bool:
    content = text.encode("utf-8")
    if path.exists() and path.read_bytes() == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(content)
    temporary.replace(path)
    return True


def prepare(component_dir: Path, lock_file: Path, project_manifest: Path,
            idf_path: Path, output_dir: Path) -> bool:
    component_dir, output_dir = component_dir.resolve(), output_dir.resolve()
    require(component_dir != output_dir and component_dir not in output_dir.parents,
            "Generated WebSocket overlay must be outside managed component sources")
    provenance = json.loads((PATCH_DIR / "provenance.json").read_text(encoding="utf-8"))
    name, version = provenance["component"], provenance["version"]
    require(scalar(read_lf(project_manifest).decode(), name, 2) == version,
            "Project WebSocket version differs from reviewed pin")
    lock = read_lf(lock_file).decode()
    blocks = re.findall(rf"(?m)^  {re.escape(name)}:\n((?:    [^\n]*\n)+)", lock)
    require(len(blocks) == 1, "WebSocket dependency missing or ambiguous in lock file")
    for key, expected, indent in (("version", version, 4),
                                  ("component_hash", provenance["component_hash"], 4),
                                  ("registry_url", provenance["registry_url"], 6),
                                  ("type", "service", 6)):
        require(scalar(blocks[0], key, indent) == expected, f"Unreviewed locked WebSocket {key}")
    require((component_dir / ".component_hash").read_text().strip()
            == provenance["component_hash"], "Managed WebSocket package hash differs")
    reviewed = {name: require_hash(component_dir / name, digest)
                for name, digest in provenance["source_hashes_lf"].items()}
    for name, digest in provenance["idf_source_hashes_lf"].items():
        require_hash(idf_path / name, digest)
    metadata = reviewed["idf_component.yml"]
    for key, expected, indent in (("version", version, 0),
                                  ("repository", provenance["repository"], 0),
                                  ("commit_sha", provenance["repository_commit"], 2),
                                  ("path", provenance["repository_path"], 2)):
        require(scalar(metadata, key, indent) == expected, f"Managed WebSocket {key} differs")

    source = reviewed["esp_websocket_client.c"]
    require(source.count(REDIRECT_START) == 1 and source.count(REDIRECT_END) == 1,
            "Reviewed WebSocket redirect boundaries differ")
    start, end = source.index(REDIRECT_START), source.index(REDIRECT_END)
    require(start < end, "Reviewed WebSocket redirect boundaries out of order")
    replacement = read_lf(PATCH_DIR / "reject_redirect.c").decode().rstrip() + "\n"
    source = source[:start] + replacement + source[end + len("#endif\n"):]
    return write_if_changed(output_dir / "esp_websocket_client.c", source)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("component-dir", "lock-file", "project-manifest", "idf-path", "output-dir"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    try:
        changed = prepare(args.component_dir, args.lock_file, args.project_manifest,
                          args.idf_path, args.output_dir)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"WebSocket redirect overlay refused: {error}", file=sys.stderr)
        return 1
    print(f"WebSocket redirect overlay {'generated' if changed else 'verified'}: {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
