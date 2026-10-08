from __future__ import annotations
import argparse
from pathlib import Path

LEGACY_SOURCE_BLOCK = '''    dev_camera_config_t *cfg = NULL;
    esp_err_t ret = esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (ret != ESP_OK || cfg == NULL) {
        ESP_LOGE(TAG, "Failed to resolve camera configuration for cleanup");
        return ret != ESP_OK ? ret : ESP_ERR_INVALID_STATE;
    }

    const esp_board_entry_desc_t *desc = esp_board_entry_find_subtype_desc("camera", cfg->sub_type);
    if (desc == NULL || desc->deinit_func == NULL) {
        ESP_LOGE(TAG, "No deinit function found for sub type '%s'", cfg->sub_type ? cfg->sub_type : "(null)");
        return ESP_ERR_NOT_SUPPORTED;
    }

    ret = desc->deinit_func(device_handle);
    if (ret != ESP_OK) {
        // Board-device ownership and its reference remain live on failure.
        // Propagate the subtype result so callers can retry its retained state.
        ESP_LOGE(TAG, "Sub device '%s' deinit failed with error: %d", cfg->sub_type, ret);
        return ret;
    }

    ESP_LOGI(TAG, "Sub device '%s' deinitialized successfully", cfg->sub_type);
    return 0;'''
LEGACY_SOURCE_REPLACEMENT = '''    dev_camera_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        const esp_board_entry_desc_t *desc = esp_board_entry_find_subtype_desc("camera", cfg->sub_type);
        if (desc && desc->deinit_func) {
            int ret = desc->deinit_func(device_handle);
            if (ret != 0) {
                ESP_LOGE(TAG, "Sub device '%s' deinit failed with error: %d", cfg->sub_type, ret);
            } else {
                ESP_LOGI(TAG, "Sub device '%s' deinitialized successfully", cfg->sub_type);
            }
        } else {
            ESP_LOGW(TAG, "No deinit function found for sub type '%s'", cfg->sub_type);
        }
    }
    device_handle = NULL;
    return 0;'''

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--variant", choices=("patched", "legacy"), required=True)
    args = parser.parse_args()
    text = args.source.read_text(encoding="utf-8")
    if args.variant == "patched":
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8", newline="\n")
        return 0
    start = text.index("    dev_camera_config_t *cfg = NULL;", text.index("int dev_camera_deinit"))
    end = text.index("\n}", start)
    reviewed = text[start:end]
    if reviewed != LEGACY_SOURCE_BLOCK:
        raise SystemExit("legacy source block drifted; refusing to generate negative control")
    text = text[:start] + LEGACY_SOURCE_REPLACEMENT + text[end:]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8", newline="\n")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
