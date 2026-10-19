"""Generate a checked ESP-MQTT custom-event overlay; never edit managed sources."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


PATCH_DIR = Path(__file__).resolve().parents[1] / "patches" / "esp_mqtt" / "1.0.0"


def read_lf(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def require_hash(path: Path, expected: str) -> str:
    content = read_lf(path)
    actual = hashlib.sha256(content).hexdigest()
    require(actual == expected, f"Unreviewed MQTT source/metadata: {path} (SHA-256 {actual})")
    return content.decode("utf-8")


def scalar(text: str, key: str, indent: int) -> str:
    matches = re.findall(rf"(?m)^{' ' * indent}{re.escape(key)}: ([^\n]+)$", text)
    require(len(matches) == 1, f"Expected exactly one {key} at indentation {indent}")
    return matches[0].strip().strip("\"'")


def replace_once(source: str, original: str, replacement: str) -> str:
    require(source.count(original) == 1, f"Reviewed MQTT boundary differs: {original[:90]!r}")
    return source.replace(original, replacement, 1)


def replace_function(source: str, first: str, following: str, patch: str) -> str:
    require(source.count(first) == 1 and source.count(following) == 1,
            "Reviewed MQTT function boundaries differ")
    start = source.index(first)
    end = source.index(following, start)
    return source[:start] + read_lf(PATCH_DIR / patch).decode().rstrip() + "\n\n" + source[end:]


def apply_task_storage(source: str) -> str:
    """每个客户端只有一个任务栈：最先预留、最后释放。"""
    source = replace_once(source,
        "static esp_err_t send_disconnect_msg(esp_mqtt_client_handle_t client);\n",
        "static esp_err_t send_disconnect_msg(esp_mqtt_client_handle_t client);\n"
        "static void esp_mqtt_task(void *pv);\n\n"
        + read_lf(PATCH_DIR / "task_storage.c").decode().rstrip() + "\n")
    source = replace_once(source,
        "    ESP_MEM_CHECK(TAG, client, return NULL);\n"
        "    if (!create_client_data(client)) {\n",
        "    ESP_MEM_CHECK(TAG, client, return NULL);\n"
        "    if (config == NULL ||\n"
        "        !rodak_mqtt_reserve_task_storage(client, config->task.stack_size)) {\n"
        "        goto _mqtt_init_failed;\n    }\n"
        "    if (!create_client_data(client)) {\n")
    # destroy 先删除任务再释放它可能访问的对象；任务存储最后释放，下一个客户端复用同一位置。
    # 初始化失败的客户端也会走 destroy，此时这些指针为 NULL。
    source = replace_once(source,
        "    if (client->run) {\n        esp_mqtt_client_stop(client);\n    }\n"
        "    esp_mqtt_destroy_config(client);\n",
        "    if (client->run) {\n        esp_mqtt_client_stop(client);\n    }\n"
        "    if (!rodak_mqtt_delete_parked_task(client)) {\n"
        '        ESP_LOGE(TAG, "Client cannot be destroyed from MQTT task");\n'
        "        return ESP_FAIL;\n    }\n"
        "    esp_mqtt_destroy_config(client);\n")
    source = replace_once(source, "    free(client->event.error_handle);\n    free(client);\n",
        "    free(client->event.error_handle);\n"
        "    heap_caps_free(client->rodak_task_buffer);\n"
        "    heap_caps_free(client->rodak_task_stack);\n"
        "    free(client);\n")
    # start 在创建任务前置 run、清 STOPPED_BIT；任务不能再做一遍，否则会覆盖首次运行前到达的 stop。
    source = replace_once(source,
        "    outbox_tick_t msg_tick = 0;\n    client->run = true;\n\n"
        "    client->state = MQTT_STATE_INIT;\n"
        "    xEventGroupClearBits(client->status_bits, STOPPED_BIT);\n"
        "    while (client->run) {\n",
        "    outbox_tick_t msg_tick = 0;\n\n"
        "    MQTT_API_LOCK(client);\n"
        "    if (client->run) {\n        client->state = MQTT_STATE_INIT;\n    }\n"
        "    MQTT_API_UNLOCK(client);\n"
        "    while (client->run) {\n")
    source = replace_once(source,
        "    xEventGroupSetBits(client->status_bits, STOPPED_BIT);\n\n    vTaskDelete(NULL);\n}\n",
        "    xEventGroupSetBits(client->status_bits, STOPPED_BIT);\n\n"
        "    /* 下一次 start 或 destroy 会先删除本任务，再复用或释放其静态存储。 */\n"
        "    vTaskSuspend(NULL);\n}\n")
    start = ("    esp_err_t err = ESP_OK;\n#if MQTT_CORE_SELECTION_ENABLED\n"
             '    ESP_LOGD(TAG, "Core selection enabled on %u", MQTT_TASK_CORE);\n'
             '    if (xTaskCreatePinnedToCore(esp_mqtt_task, "mqtt_task", client->config->task_stack, '
             "client, client->config->task_prio, &client->task_handle, MQTT_TASK_CORE) != pdTRUE) {\n"
             '        ESP_LOGE(TAG, "Error create mqtt task");\n'
             "        err = ESP_FAIL;\n    }\n#else\n"
             '    ESP_LOGD(TAG, "Core selection disabled");\n'
             '    if (xTaskCreate(esp_mqtt_task, "mqtt_task", client->config->task_stack, client, '
             "client->config->task_prio, &client->task_handle) != pdTRUE) {\n"
             '        ESP_LOGE(TAG, "Error create mqtt task");\n'
             "        err = ESP_FAIL;\n    }\n#endif\n")
    return replace_once(source, start, "    esp_err_t err = rodak_mqtt_create_task(client);\n")


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
            "Generated MQTT overlay must be outside managed component sources")
    provenance = json.loads((PATCH_DIR / "provenance.json").read_text(encoding="utf-8"))
    name, version = provenance["component"], provenance["version"]
    require(scalar(read_lf(project_manifest).decode(), name, 2) == version,
            "Project MQTT version differs from reviewed pin")
    lock = read_lf(lock_file).decode()
    blocks = re.findall(rf"(?m)^  {re.escape(name)}:\n((?:    [^\n]*\n)+)", lock)
    require(len(blocks) == 1, "MQTT dependency missing or ambiguous in lock file")
    for key, expected, indent in (("version", version, 4),
                                  ("component_hash", provenance["component_hash"], 4),
                                  ("registry_url", provenance["registry_url"], 6),
                                  ("type", "service", 6)):
        require(scalar(blocks[0], key, indent) == expected, f"Unreviewed locked MQTT {key}")
    require((component_dir / ".component_hash").read_text().strip()
            == provenance["component_hash"], "Managed MQTT package hash differs")
    reviewed = {name: require_hash(component_dir / name, digest)
                for name, digest in provenance["source_hashes_lf"].items()}
    require_hash(idf_path / "components/esp_event/esp_event.c",
                 provenance["idf_event_source_sha256_lf"])
    require(scalar(reviewed["idf_component.yml"], "version", 0) == version,
            "Managed MQTT version differs")
    require(scalar(reviewed["idf_component.yml"], "repository", 0) == provenance["repository"],
            "Managed MQTT repository differs")

    source = reviewed["mqtt_client.c"]
    source = replace_function(source,
        "esp_err_t esp_mqtt_dispatch_custom_event(esp_mqtt_client_handle_t client, esp_mqtt_event_t *event)\n",
        "static esp_err_t esp_mqtt_dispatch_event(esp_mqtt_client_handle_t client)\n{",
        "custom_dispatch.c")
    source = replace_function(source,
        "static inline void run_event_loop(esp_mqtt_client_handle_t client)\n",
        "static void esp_mqtt_task(void *pv)\n", "run_event_loop.c")
    source = replace_once(source, '#include "mqtt_client_priv.h"\n',
        '#include "mqtt_client_priv.h"\n'
        '#ifdef MQTT_DISABLE_API_LOCKS\n#error "Rodak MQTT event overlay requires API locks"\n#endif\n')
    source = replace_once(source,
        "    client->api_lock = xSemaphoreCreateRecursiveMutex();\n",
        "    client->rodak_custom_events = xQueueCreate(MQTT_EVENT_QUEUE_SIZE, sizeof(esp_mqtt_event_t));\n"
        "    ESP_MEM_CHECK(TAG, client->rodak_custom_events, return false);\n\n"
        "    client->api_lock = xSemaphoreCreateRecursiveMutex();\n")
    source = replace_once(source, "    free(client->event.error_handle);\n",
        "    if (client->rodak_custom_events) {\n"
        "        vQueueDelete(client->rodak_custom_events);\n    }\n"
        "    free(client->event.error_handle);\n")
    source = replace_once(source, "    outbox_delete_all_items(client->outbox);\n",
        "    outbox_delete_all_items(client->outbox);\n"
        "    rodak_mqtt_reset_custom_events(client);\n")
    source = replace_once(source, "    esp_err_t err = ESP_OK;\n#if MQTT_CORE_SELECTION_ENABLED",
        "    rodak_mqtt_reset_custom_events(client);\n"
        "    esp_err_t err = ESP_OK;\n#if MQTT_CORE_SELECTION_ENABLED")
    source = replace_once(source,
        "#if MQTT_EVENT_QUEUE_SIZE > 1\n    atomic_init(&client->queued_events, 0);\n#endif\n", "")
    source = replace_once(source,
        "    return\n#if MQTT_EVENT_QUEUE_SIZE > 1\n"
        "        atomic_load(&client->queued_events) > 0 ? 10 : max_timeout;\n"
        "#else\n        max_timeout;\n#endif\n",
        "    return uxQueueMessagesWaiting(client->rodak_custom_events) > 0 ? 10 : max_timeout;\n")
    source = replace_once(source,
        "// if supporting multiple queued events, we keep track of them\n"
        "                                      // using atomic variable, so need to make sure it won't get allocated in PSRAM",
        "// Retain upstream internal-memory placement for larger event queues")
    source = apply_task_storage(source)
    header = replace_once(reviewed["lib/include/mqtt_client_priv.h"],
        '#include "freertos/event_groups.h"\n',
        '#include "freertos/event_groups.h"\n#include "freertos/queue.h"\n')
    header = replace_once(header, "    SemaphoreHandle_t  api_lock;\n",
        "    QueueHandle_t      rodak_custom_events;\n    SemaphoreHandle_t  api_lock;\n")
    header = replace_once(header, "    TaskHandle_t       task_handle;\n",
        "    TaskHandle_t       task_handle;\n"
        "    StackType_t       *rodak_task_stack;\n"
        "    StaticTask_t      *rodak_task_buffer;\n"
        "    int                rodak_task_stack_size;\n")
    header = replace_once(header,
        "#if MQTT_EVENT_QUEUE_SIZE > 1\n    atomic_int         queued_events;\n#endif\n", "")
    # Validate everything before replacing either output.
    changed = write_if_changed(output_dir / "mqtt_client.c", source)
    return write_if_changed(output_dir / "mqtt_client_priv.h", header) or changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("component-dir", "lock-file", "project-manifest", "idf-path", "output-dir"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    try:
        changed = prepare(args.component_dir, args.lock_file, args.project_manifest,
                          args.idf_path, args.output_dir)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"MQTT event overlay refused: {error}", file=sys.stderr)
        return 1
    print(f"MQTT event overlay {'generated' if changed else 'verified'}: {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
