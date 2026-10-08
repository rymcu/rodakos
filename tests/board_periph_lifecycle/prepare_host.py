"""Prepare patched and legacy host copies of the production source."""

from __future__ import annotations

import argparse
from pathlib import Path


PATCHED_BLOCK = '''    if (list->ref_count == 0) {
        /* Check if deinit function exists */
        if (!handle->deinit) {
            list->ref_count++;
            ESP_LOGE(TAG, "No deinit function for periph: %s", name);
            return ESP_BOARD_ERR_PERIPH_NO_INIT;
        }

        /* Deinitialize peripheral */
        esp_err_t ret = handle->deinit(list->periph_handle);
        if (ret != ESP_OK) {
            list->ref_count++;
            ESP_LOGE(TAG, "Failed to deinit periph: %s", name);
            return ret;
        }

        list->periph_handle = NULL;
'''

LEGACY_BLOCK = '''    if (list->ref_count == 0) {
        /* Check if deinit function exists */
        ESP_BOARD_RETURN_ON_FALSE(handle->deinit, ESP_BOARD_ERR_PERIPH_NO_INIT, TAG,
                                  "No deinit function for periph: %s", name);

        /* Deinitialize peripheral */
        esp_err_t ret = handle->deinit(list->periph_handle);
        ESP_BOARD_RETURN_ON_ERROR(ret, TAG, "Failed to deinit periph: %s", name);

        list->periph_handle = NULL;
'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--variant", choices=("patched", "legacy"), required=True)
    args = parser.parse_args()

    source = args.source.read_text(encoding="utf-8")
    if PATCHED_BLOCK not in source:
        raise SystemExit("production source does not contain the reviewed ref-count repair")
    if args.variant == "legacy":
        source = source.replace(PATCHED_BLOCK, LEGACY_BLOCK, 1)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
