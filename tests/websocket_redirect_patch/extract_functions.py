"""Compile original task/abort/stop bodies from the verified production overlay and upstream."""

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from prepare_websocket_redirect_patch import prepare, read_lf, require, write_if_changed


def between(source: str, first: str, following: str) -> str:
    require(source.count(first) == 1 and source.count(following) == 1,
            "Reviewed WebSocket test extraction boundaries differ")
    return source[source.index(first):source.index(following, source.index(first))]


def functions(source: str) -> str:
    return "\n".join([
        between(source, "static esp_err_t esp_websocket_client_abort_connection(",
                "static char *http_auth_basic("),
        between(source, "static esp_err_t stop_wait_task(",
                "#if WS_TRANSPORT_HEADER_CALLBACK_SUPPORT\nstatic void websocket_header_hook("),
        between(source, "static void esp_websocket_client_task(void *pv)\n",
                "esp_err_t esp_websocket_client_start("),
        between(source, "esp_err_t esp_websocket_client_stop(",
                "static int esp_websocket_client_send_close(esp_websocket_client_handle_t client, int code, const char *additional_data, int total_len, TickType_t timeout)\n{"),
    ])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--idf-path", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    component = ROOT / "managed_components/espressif__esp_websocket_client"
    prepare(component, ROOT / "dependencies.lock", ROOT / "main/idf_component.yml",
            args.idf_path, args.output_dir)
    original = read_lf(component / "esp_websocket_client.c").decode()
    patched = read_lf(args.output_dir / "esp_websocket_client.c").decode()
    header = read_lf(component / "include/esp_websocket_client.h").decode()
    types = between(original, "typedef struct {\n    BaseType_t", "static uint64_t _tick_get_ms(")
    errors = between(header, "typedef enum {\n    WEBSOCKET_ERROR_TYPE_NONE", "/**\n * @brief Websocket event data")
    write_if_changed(args.output_dir / "ws_types.inc", errors + "\n" + types)
    write_if_changed(args.output_dir / "ws_functions.inc", functions(patched))
    write_if_changed(args.output_dir / "ws_upstream_functions.inc", functions(original))


if __name__ == "__main__":
    main()
