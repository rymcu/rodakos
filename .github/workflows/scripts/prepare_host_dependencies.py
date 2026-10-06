"""Fetch only locked host-test sources; never resolve or rewrite the firmware lock."""

import argparse
import hashlib
from importlib.metadata import version
from pathlib import Path

from idf_component_tools.hash_tools.validate import validate_hash_eq_hashdir
from idf_component_tools.manifest import SolvedComponent
from idf_component_tools.sources.fetcher import ComponentFetcher
from ruamel.yaml import YAML


HOST_COMPONENTS = (
    "chmorgan/esp-libhelix-mp3",
    "espressif/cjson",
    "espressif/esp_codec_dev",
    "espressif/esp_websocket_client",
    "espressif/mqtt",
    "lvgl/lvgl",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    destination = args.destination or root / "managed_components"
    if version("idf-component-manager") != "3.0.3":
        raise RuntimeError("Host dependency bootstrap requires idf-component-manager 3.0.3")
    lock_path = root / "dependencies.lock"
    original_lock = lock_path.read_bytes()
    lock = YAML(typ="safe").load(original_lock)
    for name in HOST_COMPONENTS:
        locked = lock["dependencies"][name]
        if locked["source"] != {
            "registry_url": "https://components.espressif.com/",
            "type": "service",
        }:
            raise RuntimeError(f"Unreviewed host component source: {name}")
        component = SolvedComponent(name=name, **locked)
        fetched = ComponentFetcher(component, destination).download()
        if not fetched:
            raise RuntimeError(f"Host component was not downloaded: {name}")
        # Validate the bytes even after a cold download, not merely .component_hash.
        validate_hash_eq_hashdir(fetched, component.component_hash)
        print(f"Verified {name}@{component.version}: {component.component_hash}", flush=True)
    if lock_path.read_bytes() != original_lock:
        raise RuntimeError("Host dependency bootstrap changed dependencies.lock")
    print(f"dependencies.lock SHA-256: {hashlib.sha256(original_lock).hexdigest()}")


if __name__ == "__main__":
    main()
