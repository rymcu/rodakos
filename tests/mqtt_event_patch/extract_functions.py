"""Compile the exact reviewed dispatch functions with host queue/event fakes."""

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from prepare_mqtt_event_patch import prepare, read_lf


def between(source: str, first: str, following: str) -> str:
    assert source.count(first) == 1 and source.count(following) == 1
    return source[source.index(first):source.index(following, source.index(first))]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--idf-path", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    component = ROOT / "managed_components/espressif__mqtt"
    prepare(component, ROOT / "dependencies.lock", ROOT / "main/idf_component.yml",
            args.idf_path, args.output_dir)
    patched = (args.output_dir / "mqtt_client.c").read_text()
    original = read_lf(component / "mqtt_client.c").decode()
    native_start = "static esp_err_t esp_mqtt_dispatch_event(esp_mqtt_client_handle_t client)\n{"
    custom_start = "esp_err_t esp_mqtt_dispatch_custom_event(esp_mqtt_client_handle_t client, esp_mqtt_event_t *event)\n"
    pieces = [
        between(patched, "static void rodak_mqtt_reset_custom_events(", native_start),
        between(patched, native_start, "static esp_err_t deliver_publish("),
        between(patched, "static inline int max_poll_timeout(", "static void esp_mqtt_task(void *pv)\n"),
        between(original, custom_start, native_start).replace(
            "esp_mqtt_dispatch_custom_event", "upstream_dispatch_custom_event"),
    ]
    (args.output_dir / "mqtt_event_functions.inc").write_text("\n".join(pieces), encoding="utf-8")


if __name__ == "__main__":
    main()
