"""Generate pinned Camera diagnostics and cooperative DVP shutdown without editing managed sources."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from ruamel.yaml import YAML
from ruamel.yaml.error import YAMLError


PATCH_DIR = Path(__file__).resolve().parents[1] / "patches" / "camera_teardown" / "2.3.0"
VIDEO_SOURCE = "src/device/esp_video_device_common.c"
SENSOR_SOURCE = "src/driver_dvp/esp_cam_ctlr_dvp_cam.c"


def read_lf(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def require_hash(path: Path, expected: str) -> str:
    content = read_lf(path)
    actual = hashlib.sha256(content).hexdigest()
    require(actual == expected, f"Unreviewed camera source/metadata: {path} (SHA-256 {actual})")
    return content.decode("utf-8")


def lock_graph_digest(path: Path) -> str:
    try:
        locked = YAML(typ="safe").load(read_lf(path).decode("utf-8"))
        require(isinstance(locked, dict), "Camera dependency lock must be a mapping")
        # Match firmware_ci.verify_dependency_graph: only this derived top-level
        # field may vary after Board Manager resolves on a different platform.
        graph = {key: value for key, value in locked.items() if key != "manifest_hash"}
        canonical = json.dumps(graph, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    except (YAMLError, TypeError) as error:
        raise ValueError(f"Invalid camera dependency graph: {error}") from error
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def replace_exact(source: str, original: str, replacement: str, count: int = 1) -> str:
    require(source.count(original) == count,
            f"Expected {count} reviewed camera boundaries: {original[:100]!r}")
    return source.replace(original, replacement)


def function_text(source: str, name: str) -> str:
    definitions = list(re.finditer(r'(?m)^(?:static )?(?:IRAM_ATTR )?\w+\s+' + re.escape(name) + r'\(', source))
    require(len(definitions) == 1, f"Expected one function definition: {name}")
    start = definitions[0].start()
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError(f"Unclosed reviewed camera function: {name}")


def mark(phase: str, status: str = "0", indent: str = "    ") -> str:
    return (f"{indent}(void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_{phase},\n"
            f"{indent}    (uint32_t)xPortGetCoreID(), {status});\n")


def require_controller_stop_before_sensor_stop(source: str) -> None:
    common = function_text(source, "common_video_stop")
    controller = common.find("esp_cam_ctlr_stop(common->cam_ctrl_handle)")
    sensor = common.find("esp_cam_sensor_ioctl(common->cam.sensor")
    require(controller >= 0 and sensor >= 0 and controller < sensor,
            "Camera controller must stop before the sensor STREAMOFF command")


def instrument_video(source: str) -> str:
    require("rodak_camera_teardown_" not in source, "Video source already contains diagnostics")
    source = replace_exact(source, '#include "esp_check.h"\n',
                           '#include "esp_check.h"\n#include "freertos/FreeRTOS.h"\n'
                           '#include "camera-teardown-diagnostics.h"\n')
    original = function_text(source, "common_video_stop")
    patched = '''static esp_err_t common_video_stop(struct esp_video *video, uint32_t type)
{
    esp_video_device_common_t *common = VIDEO_DEVICE_COMMON(video);

    assert(common->cam.sensor);

    int flags = 0;
    (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_STOP_ENTER,
        (uint32_t)xPortGetCoreID(), 0);
    esp_err_t rodak_teardown_result = esp_cam_ctlr_stop(common->cam_ctrl_handle);
    (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_STOP_RETURNED,
        (uint32_t)xPortGetCoreID(), rodak_teardown_result);
    ESP_RETURN_ON_ERROR(rodak_teardown_result, TAG, "failed to stop CAM ctlr");

    (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_SENSOR_ENTER,
        (uint32_t)xPortGetCoreID(), 0);
    rodak_teardown_result = esp_cam_sensor_ioctl(common->cam.sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &flags);
    (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_SENSOR_RETURNED,
        (uint32_t)xPortGetCoreID(), rodak_teardown_result);
    ESP_RETURN_ON_ERROR(rodak_teardown_result, TAG, "failed to stop sensor stream");

    if (common->intf->stop) {
        ESP_RETURN_ON_ERROR(common->intf->stop(common), TAG, "device stop failed");
    }
    (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DISABLE_ENTER,
        (uint32_t)xPortGetCoreID(), 0);
    rodak_teardown_result = esp_cam_ctlr_disable(common->cam_ctrl_handle);
    (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DISABLE_RETURNED,
        (uint32_t)xPortGetCoreID(), rodak_teardown_result);
    ESP_RETURN_ON_ERROR(rodak_teardown_result, TAG, "failed to disable CAM ctlr");

    (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DEL_ENTER,
        (uint32_t)xPortGetCoreID(), 0);
    rodak_teardown_result = esp_cam_ctlr_del(common->cam_ctrl_handle);
    (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DEL_RETURNED,
        (uint32_t)xPortGetCoreID(), rodak_teardown_result);
    ESP_RETURN_ON_ERROR(rodak_teardown_result, TAG, "failed to delete CAM ctlr");
    common->cam_ctrl_handle = NULL;

    return ESP_OK;
}'''
    require(patched.count("rodak_camera_teardown_record(") == 8, "Video mark budget changed")
    source = replace_exact(source, original, patched)
    require_controller_stop_before_sensor_stop(source)
    return source


def instrument_sensor(source: str) -> str:
    require("rodak_camera_teardown_" not in source, "Sensor source already contains diagnostics")
    source = replace_exact(source, '#include "esp_check.h"\n',
                           '#include "esp_check.h"\n#include <stdbool.h>\n'
                           '#include "camera-teardown-diagnostics.h"\n')
    source = replace_exact(source,
        '    TaskHandle_t task_handle;                           /*!< DVP task handle */',
        '    TaskHandle_t task_handle;                           /*!< DVP task handle */\n'
        '    bool teardown_started;                            /*!< Owner entered staged deletion */\n'
        '    bool teardown_task_deleted;                       /*!< Owner completed worker deletion */\n'
        '    bool teardown_gpio_disabled;                      /*!< V-SYNC interrupt disabled */\n'
        '    bool teardown_capture_stopped;                    /*!< DVP/GDMA capture stopped */\n'
        '    bool teardown_hal_deinitialized;                  /*!< CAM HAL deinitialized */\n'
        '    bool teardown_gpio_removed;                       /*!< V-SYNC ISR removed */\n'
        '    bool teardown_dma_disconnected;                   /*!< GDMA disconnected */\n'
        '    bool teardown_dma_deleted;                        /*!< GDMA channel deleted */\n'
        '    bool teardown_task_marked;                        /*!< Diagnostics task entry emitted */\n'
        '    bool teardown_gpio_disable_marked;                /*!< Diagnostics GPIO disable entry emitted */\n'
        '    bool teardown_capture_marked;                     /*!< Diagnostics capture entry emitted */\n'
        '    bool teardown_gpio_remove_marked;                 /*!< Diagnostics GPIO remove entry emitted */\n'
        '    bool teardown_dma_disconnect_marked;              /*!< Diagnostics GDMA disconnect entry emitted */\n'
        '    bool teardown_dma_delete_marked;                  /*!< Diagnostics GDMA delete entry emitted */\n'
        '    bool stream_stop_requested;                       /*!< Owner is stopping capture */\n'
        '')
    original = function_text(source, "dvp_dma_deinit")
    patched = replace_exact(original,
        "dvp_dma_deinit(gdma_channel_handle_t gdma_chan)",
        "dvp_dma_deinit(gdma_channel_handle_t gdma_chan, bool record_teardown)")
    for phase, function, error in (("DISCONNECT", "gdma_disconnect", "disconnect dma channel failed"),
                                   ("DELETE", "gdma_del_channel", "delete dma channel failed")):
        declaration = "esp_err_t " if phase == "DISCONNECT" else ""
        patched = replace_exact(patched,
            f'        ESP_RETURN_ON_ERROR({function}(gdma_chan), TAG, "{error}");',
            "        if (record_teardown) {\n" + mark("DVP_GDMA_" + phase + "_ENTER", indent="            ") +
            "        }\n" + f"        {declaration}rodak_teardown_result = {function}(gdma_chan);\n" +
            "        if (record_teardown) {\n" + mark("DVP_GDMA_" + phase + "_RETURNED", "rodak_teardown_result", "            ") +
            "        }\n" + f'        ESP_RETURN_ON_ERROR(rodak_teardown_result, TAG, "{error}");')
    source = replace_exact(source, original, patched)
    source = replace_exact(source, "    dvp_dma_deinit(*gdma_chan);", "    dvp_dma_deinit(*gdma_chan, false);")
    source = replace_exact(source, "    dvp_dma_deinit(ctlr->dma_chan);", "    dvp_dma_deinit(ctlr->dma_chan, false);")
    original = function_text(source, "esp_cam_new_dvp_ctlr_ext")
    patched = replace_exact(original,
        '''    size_t dma_buffer_max_size = DVP_CAM_DMA_BUFFER_SIZE;''',
        '''    const size_t dma_buffer_candidates[] = {DVP_CAM_DMA_BUFFER_SIZE, 6144, 4096};''')
    patched = replace_exact(patched,
        '''    ctlr->dma_buffer_hsize = dvp_get_dma_buffer_hsize(dma_buffer_max_size, buffer_align_size, fb_size_in_bytes, config->pic_format_jpeg);
    ESP_GOTO_ON_FALSE(ctlr->dma_buffer_hsize > 0, ESP_ERR_INVALID_ARG, fail0, TAG, "invalid argument: dma_buffer_hsize is 0");
    ctlr->dma_buffer_size = ctlr->dma_buffer_hsize * DVP_CAM_BUFFER_COUNT;

    ctlr->dma_buffer = heap_caps_aligned_alloc(buffer_align_size, ctlr->dma_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_GOTO_ON_FALSE(ctlr->dma_buffer, ESP_ERR_NO_MEM, fail0, TAG, "no mem for CAM DVP DMA receive buffer");
    memset(ctlr->dma_buffer, 0, ctlr->dma_buffer_size);

    ctlr->dma_desc_size = config->pic_format_jpeg ? DVP_CAM_JPEG_DMA_DESC_SIZE : DVP_CAM_DMA_DESC_BUFFER_SIZE;
    ctlr->dma_desc_hcnt = (ctlr->dma_buffer_hsize + ctlr->dma_desc_size - 1) / ctlr->dma_desc_size;

    size_t dma_desc_buffer_size = DVP_CAM_UP_ALIGN(DVP_CAM_BUFFER_COUNT * ctlr->dma_desc_hcnt * sizeof(dma_descriptor_t), buffer_align_size);
    ctlr->dma_desc = heap_caps_aligned_alloc(buffer_align_size, dma_desc_buffer_size, MALLOC_CAP_DMA);
    ESP_GOTO_ON_FALSE(ctlr->dma_desc, ESP_ERR_NO_MEM, fail1, TAG, "no mem for CAM DVP DMA receive description");''',
        '''    ctlr->dma_desc_size = config->pic_format_jpeg ? DVP_CAM_JPEG_DMA_DESC_SIZE : DVP_CAM_DMA_DESC_BUFFER_SIZE;
    for (size_t candidate_index = 0; candidate_index < sizeof(dma_buffer_candidates) / sizeof(dma_buffer_candidates[0]); ++candidate_index) {
        const size_t candidate_size = dma_buffer_candidates[candidate_index];
        if (candidate_size > DVP_CAM_DMA_BUFFER_SIZE) {
            continue;
        }
        const size_t candidate_hsize = dvp_get_dma_buffer_hsize(candidate_size, buffer_align_size,
                                                                 fb_size_in_bytes, config->pic_format_jpeg);
        if (candidate_hsize == 0) {
            continue;
        }
        ctlr->dma_buffer_hsize = candidate_hsize;
        ctlr->dma_buffer_size = candidate_hsize * DVP_CAM_BUFFER_COUNT;
        ctlr->dma_buffer = heap_caps_aligned_alloc(buffer_align_size, ctlr->dma_buffer_size,
                                                   MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (ctlr->dma_buffer == NULL) {
            continue;
        }
        ctlr->dma_desc_hcnt = (candidate_hsize + ctlr->dma_desc_size - 1) / ctlr->dma_desc_size;
        const size_t dma_desc_buffer_size = DVP_CAM_UP_ALIGN(
            DVP_CAM_BUFFER_COUNT * ctlr->dma_desc_hcnt * sizeof(dma_descriptor_t), buffer_align_size);
        ctlr->dma_desc = heap_caps_aligned_alloc(buffer_align_size, dma_desc_buffer_size, MALLOC_CAP_DMA);
        if (ctlr->dma_desc != NULL) {
            if (candidate_size != DVP_CAM_DMA_BUFFER_SIZE) {
                ESP_LOGW(TAG, "DVP DMA ring fallback: configured=%u selected=%u",
                         (unsigned)DVP_CAM_DMA_BUFFER_SIZE, (unsigned)candidate_size);
            }
            break;
        }
        heap_caps_free(ctlr->dma_buffer);
        ctlr->dma_buffer = NULL;
    }
    if (ctlr->dma_buffer == NULL || ctlr->dma_desc == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto fail0;
    }
    memset(ctlr->dma_buffer, 0, ctlr->dma_buffer_size);''')
    patched = replace_exact(patched,
        '''fail2:
    heap_caps_free(ctlr->dma_desc);
fail1:
    heap_caps_free(ctlr->dma_buffer);''',
        '''fail2:
    heap_caps_free(ctlr->dma_desc);
    heap_caps_free(ctlr->dma_buffer);''')
    source = replace_exact(source, original, patched)
    original = function_text(source, "dvp_cam_ctlr_stop")
    patched = replace_exact(original,
        '''    ctlr->dvp_fsm = DVP_CAM_FSM_INIT;
    ret = gpio_intr_disable(ctlr->vsync_pin);
    if (ret == ESP_OK) {
        ret = dvp_stop_capturing(ctlr);
    }

    return ret;''',
        '''    portENTER_CRITICAL(&ctlr->spinlock);
    ctlr->stream_stop_requested = true;
    ctlr->dvp_fsm = DVP_CAM_FSM_INIT;
    portEXIT_CRITICAL(&ctlr->spinlock);
    if (!ctlr->teardown_gpio_disabled) {
        ret = gpio_intr_disable(ctlr->vsync_pin);
        if (ret != ESP_OK) {
            return ret;
        }
        ctlr->teardown_gpio_disabled = true;
    }
    if (!ctlr->teardown_capture_stopped) {
        ret = dvp_stop_capturing(ctlr);
        if (ret != ESP_OK) {
            return ret;
        }
        ctlr->teardown_capture_stopped = true;
    }

    return ESP_OK;''')
    source = replace_exact(source, original, patched)
    original = function_text(source, "dvp_cam_ctlr_del")
    patched = replace_exact(original, "    vTaskDelete(ctlr->task_handle);",
        mark("DVP_TASK_DELETE_ENTER") + "    vTaskDelete(ctlr->task_handle);\n" +
        mark("DVP_TASK_DELETE_RETURNED").rstrip())
    for phase, call in (("GPIO_DISABLE", "gpio_intr_disable(ctlr->vsync_pin)"),
                         ("CAPTURE_STOP", "dvp_stop_capturing(ctlr)"),
                         ("GPIO_REMOVE", "gpio_isr_handler_remove(ctlr->vsync_pin)")):
        declaration = "esp_err_t " if phase == "GPIO_DISABLE" else ""
        patched = replace_exact(patched, f"    {call};",
            mark("DVP_" + phase + "_ENTER") +
            f"    {declaration}rodak_teardown_result = {call};\n" +
            mark("DVP_" + phase + "_RETURNED", "rodak_teardown_result").rstrip())
    patched = replace_exact(patched, "dvp_dma_deinit(ctlr->dma_chan)", "dvp_dma_deinit(ctlr->dma_chan, true)")
    source = replace_exact(source, original, patched)
    original = function_text(source, "dvp_cam_ctlr_start")
    patched = replace_exact(original,
        '    dvp_cam_ctlr_t *ctlr = (dvp_cam_ctlr_t *)handle;\n\n    ctlr->dvp_fsm = DVP_CAM_FSM_STARTED;',
        '    dvp_cam_ctlr_t *ctlr = (dvp_cam_ctlr_t *)handle;\n\n'
        '    if (ctlr->teardown_started) {\n'
        '        return ESP_ERR_INVALID_STATE;\n'
        '    }\n'
        '    ctlr->teardown_gpio_disabled = false;\n'
        '    ctlr->teardown_capture_stopped = false;\n'
        '    portENTER_CRITICAL(&ctlr->spinlock);\n'
        '    ctlr->stream_stop_requested = false;\n'
        '    ctlr->dvp_fsm = DVP_CAM_FSM_STARTED;\n'
        '    portEXIT_CRITICAL(&ctlr->spinlock);')
    patched = replace_exact(patched,
        '''    if (ret != ESP_OK) {
        ctlr->dvp_fsm = DVP_CAM_FSM_INIT;
        ESP_EARLY_LOGE(TAG, "failed to enable vsync interrupt");
    }''',
        '''    if (ret != ESP_OK) {
        portENTER_CRITICAL(&ctlr->spinlock);
        ctlr->dvp_fsm = DVP_CAM_FSM_INIT;
        ctlr->stream_stop_requested = true;
        portEXIT_CRITICAL(&ctlr->spinlock);
        ESP_EARLY_LOGE(TAG, "failed to enable vsync interrupt");
    }''')
    source = replace_exact(source, original, patched)
    original = function_text(source, "dvp_cam_ctlr_register_event_callbacks")
    patched = replace_exact(original,
        '    ESP_RETURN_ON_FALSE(handle && cbs, ESP_ERR_INVALID_ARG, TAG, "invalid argument: handle or cbs is null");',
        '    ESP_RETURN_ON_FALSE(handle && cbs, ESP_ERR_INVALID_ARG, TAG, "invalid argument: handle or cbs is null");\n'
        '    ESP_RETURN_ON_FALSE(!ctlr->teardown_started, ESP_ERR_INVALID_STATE, TAG, "controller teardown is pending");')
    source = replace_exact(source, original, patched)
    # At this intermediate boundary the reviewed controller instrumentation
    # still contains twelve markers.  Worker lifecycle instrumentation below
    # replaces the delete body and validates the final sixteen-marker source.
    require(source.count("rodak_camera_teardown_record(") == 12, "DVP mark budget changed")
    require(source.count("dvp_dma_deinit(") == 4, "DVP cleanup call sites changed")
    return instrument_worker_lifecycle(source)


def instrument_worker_lifecycle(source: str) -> str:
    source = replace_exact(source, '#include "freertos/task.h"\n',
                           '#include "freertos/task.h"\n#include "freertos/idf_additions.h"\n')
    source = replace_exact(source, '    DVP_CAM_EVENT_RECV_DATA = 1,',
                           '    DVP_CAM_EVENT_SHUTDOWN = 2,\n    DVP_CAM_EVENT_RECV_DATA = 1,')
    source = replace_exact(source,
        '    bool teardown_dma_deleted;                        /*!< GDMA channel deleted */',
        '    bool teardown_dma_deleted;                        /*!< GDMA channel deleted */\n'
        '    bool shutdown_requested;                         /*!< Protected by spinlock */\n'
        '    bool worker_quiesced;                            /*!< Last controller access completed */')
    helpers = '''static bool dvp_worker_shutdown_requested(dvp_cam_ctlr_t *ctlr)
{
    portENTER_CRITICAL(&ctlr->spinlock);
    bool requested = ctlr->shutdown_requested;
    portEXIT_CRITICAL(&ctlr->spinlock);
    return requested;
}

static bool dvp_stream_stop_requested(dvp_cam_ctlr_t *ctlr)
{
    portENTER_CRITICAL(&ctlr->spinlock);
    bool requested = ctlr->stream_stop_requested;
    portEXIT_CRITICAL(&ctlr->spinlock);
    return requested;
}

static void dvp_worker_quiesce(dvp_cam_ctlr_t *ctlr)
{
    portENTER_CRITICAL(&ctlr->spinlock);
    ctlr->shutdown_requested = true;
    portEXIT_CRITICAL(&ctlr->spinlock);

    // A full queue already wakes the worker. Never wait for another slot while
    // the worker may still be finishing an admitted callback or logging call.
    dvp_cam_event_t event = {.type = DVP_CAM_EVENT_SHUTDOWN};
    (void)xQueueSendToFront(ctlr->event_queue, &event, 0);
    while (true) {
        portENTER_CRITICAL(&ctlr->spinlock);
        bool quiesced = ctlr->worker_quiesced;
        portEXIT_CRITICAL(&ctlr->spinlock);
        if (quiesced) {
            break;
        }
        vTaskDelay(1);
    }
}

'''
    anchor = 'static esp_err_t dvp_cam_ctlr_del(esp_cam_ctlr_handle_t handle)'
    source = replace_exact(source, anchor, helpers + anchor)
    original = function_text(source, 'dvp_cam_ctlr_del')
    patched = '''static esp_err_t dvp_cam_ctlr_del(esp_cam_ctlr_handle_t handle)
{
    esp_err_t ret;
    dvp_cam_ctlr_t *ctlr = (dvp_cam_ctlr_t *)handle;

    if (ctlr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Delete the worker exactly once. */
    if (!ctlr->teardown_task_deleted) {
        TaskHandle_t worker = ctlr->task_handle;
        // Reject self deletion before taking any lock; callbacks may hold it.
        if (worker == NULL || worker == xTaskGetCurrentTaskHandle()) {
            return ESP_ERR_INVALID_STATE;
        }
        ctlr->teardown_started = true;
        const bool record_task = !ctlr->teardown_task_marked;
        if (record_task) {
            ctlr->teardown_task_marked = true;
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_TASK_DELETE_ENTER,
                (uint32_t)xPortGetCoreID(), 0);
        }
        dvp_worker_quiesce(ctlr);
        vTaskDeleteWithCaps(worker);
        ctlr->task_handle = NULL;
        ctlr->teardown_task_deleted = true;
        if (record_task) {
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_TASK_DELETE_RETURNED,
                (uint32_t)xPortGetCoreID(), 0);
        }
    }

    if (!ctlr->teardown_gpio_disabled) {
        const bool record_gpio_disable = !ctlr->teardown_gpio_disable_marked;
        if (record_gpio_disable) {
            ctlr->teardown_gpio_disable_marked = true;
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_GPIO_DISABLE_ENTER,
                (uint32_t)xPortGetCoreID(), 0);
        }
        ret = gpio_intr_disable(ctlr->vsync_pin);
        if (record_gpio_disable) {
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_GPIO_DISABLE_RETURNED,
                (uint32_t)xPortGetCoreID(), ret);
        }
        if (ret != ESP_OK) {
            return ret;
        }
        ctlr->teardown_gpio_disabled = true;
    }

    if (!ctlr->teardown_capture_stopped) {
        const bool record_capture = !ctlr->teardown_capture_marked;
        if (record_capture) {
            ctlr->teardown_capture_marked = true;
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_CAPTURE_STOP_ENTER,
                (uint32_t)xPortGetCoreID(), 0);
        }
        ret = dvp_stop_capturing(ctlr);
        if (record_capture) {
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_CAPTURE_STOP_RETURNED,
                (uint32_t)xPortGetCoreID(), ret);
        }
        if (ret != ESP_OK) {
            return ret;
        }
        ctlr->teardown_capture_stopped = true;
    }
    if (!ctlr->teardown_hal_deinitialized) {
        cam_hal_deinit(&ctlr->hal);
        ctlr->teardown_hal_deinitialized = true;
    }

    if (!ctlr->teardown_gpio_removed) {
        const bool record_gpio_remove = !ctlr->teardown_gpio_remove_marked;
        if (record_gpio_remove) {
            ctlr->teardown_gpio_remove_marked = true;
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_GPIO_REMOVE_ENTER,
                (uint32_t)xPortGetCoreID(), 0);
        }
        ret = gpio_isr_handler_remove(ctlr->vsync_pin);
        if (record_gpio_remove) {
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_GPIO_REMOVE_RETURNED,
                (uint32_t)xPortGetCoreID(), ret);
        }
        if (ret != ESP_OK) {
            return ret;
        }
        ctlr->teardown_gpio_removed = true;
    }

    if (ctlr->dma_chan != NULL && !ctlr->teardown_dma_disconnected) {
        const bool record_dma_disconnect = !ctlr->teardown_dma_disconnect_marked;
        if (record_dma_disconnect) {
            ctlr->teardown_dma_disconnect_marked = true;
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_GDMA_DISCONNECT_ENTER,
                (uint32_t)xPortGetCoreID(), 0);
        }
        ret = gdma_disconnect(ctlr->dma_chan);
        if (record_dma_disconnect) {
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_GDMA_DISCONNECT_RETURNED,
                (uint32_t)xPortGetCoreID(), ret);
        }
        if (ret != ESP_OK) {
            return ret;
        }
        ctlr->teardown_dma_disconnected = true;
    }
    if (ctlr->dma_chan != NULL && !ctlr->teardown_dma_deleted) {
        const bool record_dma_delete = !ctlr->teardown_dma_delete_marked;
        if (record_dma_delete) {
            ctlr->teardown_dma_delete_marked = true;
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_GDMA_DELETE_ENTER,
                (uint32_t)xPortGetCoreID(), 0);
        }
        ret = gdma_del_channel(ctlr->dma_chan);
        if (record_dma_delete) {
            (void)rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_DVP_GDMA_DELETE_RETURNED,
                (uint32_t)xPortGetCoreID(), ret);
        }
        if (ret != ESP_OK) {
            return ret;
        }
        ctlr->teardown_dma_deleted = true;
        ctlr->dma_chan = NULL;
    }

    /* Free memory only after every external owner has been released. */
    heap_caps_free(ctlr->dma_desc);
    heap_caps_free(ctlr->dma_buffer);
    vQueueDelete(ctlr->event_queue);
    heap_caps_free(ctlr);
    return ESP_OK;
}'''
    source = replace_exact(source, original, patched)
    signature = 'static IRAM_ATTR void dvp_task(void *p)'
    start = source.index(signature)
    end = source.index('\n/**', start)
    original = source[start:end]
    patched = replace_exact(original, '''    while (1) {
        if (xQueueReceive(ctlr->event_queue, &event, portMAX_DELAY) != pdPASS) {''',
        '''    while (1) {
        if (dvp_worker_shutdown_requested(ctlr)) {
            break;
        }
        BaseType_t received = xQueueReceive(ctlr->event_queue, &event, portMAX_DELAY);
        if (dvp_worker_shutdown_requested(ctlr)) {
            break;
        }
        if (received != pdPASS) {''')
    patched = patched.replace(
        '''        if (received != pdPASS) {
            ESP_LOGE(TAG, "failed to receive message");
            continue;
        }

        switch (event.type) {''',
        '''        if (received != pdPASS) {
            ESP_LOGE(TAG, "failed to receive message");
            continue;
        }
        if (dvp_stream_stop_requested(ctlr)) {
            continue;
        }

        switch (event.type) {''', 1)
    patched = patched.replace(
        'if (ctlr->dvp_fsm == DVP_CAM_FSM_RXING) {',
        'if (!dvp_stream_stop_requested(ctlr) && ctlr->dvp_fsm == DVP_CAM_FSM_RXING) {', 1)
    patched = replace_exact(patched,
        '                    DVP_CAM_ERROR("RX-DA OVF");',
        '''                    if (!dvp_stream_stop_requested(ctlr)) {
                        DVP_CAM_ERROR("RX-DA OVF");
                    }''')
    patched = patched.replace(
        '} else if (ctlr->dvp_fsm == DVP_CAM_FSM_RXING) {',
        '} else if (!dvp_stream_stop_requested(ctlr) && ctlr->dvp_fsm == DVP_CAM_FSM_RXING) {', 1)
    patched = patched.replace(
        'if (trans->buffer && trans->buflen > 0) {',
        'if (!dvp_stream_stop_requested(ctlr) && trans->buffer && trans->buflen > 0) {')
    patched = patched.replace(
        'if (ctlr->dvp_fsm == DVP_CAM_FSM_STARTED) {',
        'if (!dvp_stream_stop_requested(ctlr) && ctlr->dvp_fsm == DVP_CAM_FSM_STARTED) {', 1)
    patched = replace_exact(patched,
        '                    DVP_CAM_ERROR("RX-SV OVF");',
        '''                    if (!dvp_stream_stop_requested(ctlr)) {
                        DVP_CAM_ERROR("RX-SV OVF");
                    }''')
    patched = replace_exact(patched,
        '''                        if (ctlr->fb_size_in_bytes != trans->received_size) {
                            DVP_CAM_ERROR("RX:%d-%d", (int)ctlr->fb_size_in_bytes, (int)trans->received_size);
                            trans->received_size = 0;
                        }''',
        '''                        if (ctlr->fb_size_in_bytes != trans->received_size) {
                            if (!dvp_stream_stop_requested(ctlr)) {
                                DVP_CAM_ERROR("RX:%d-%d", (int)ctlr->fb_size_in_bytes, (int)trans->received_size);
                            }
                            trans->received_size = 0;
                        }''')
    patched = patched.replace(
        '                if (trans->received_size) {',
        '                if (trans->received_size && !dvp_stream_stop_requested(ctlr)) {', 1)
    patched = replace_exact(patched,
        '''                    } else {
                        ctlr->dvp_fsm = DVP_CAM_FSM_STARTED;
                    }''',
        '''                    } else if (!dvp_stream_stop_requested(ctlr)) {
                        ctlr->dvp_fsm = DVP_CAM_FSM_STARTED;
                    }''')
    patched = patched.replace(
        '''                } else {
                    trans->received_size = 0;

                    ctlr->dma_desc_index = 0;
                    ctlr->dvp_fsm = DVP_CAM_FSM_RXING;
                    dvp_start_capturing(ctlr);
                }

                gpio_intr_enable(ctlr->vsync_pin);''',
        '''                } else if (!dvp_stream_stop_requested(ctlr)) {
                    trans->received_size = 0;

                    ctlr->dma_desc_index = 0;
                    ctlr->dvp_fsm = DVP_CAM_FSM_RXING;
                    dvp_start_capturing(ctlr);
                }

                if (!dvp_stream_stop_requested(ctlr)) {
                    gpio_intr_enable(ctlr->vsync_pin);
                    if (dvp_stream_stop_requested(ctlr)) {
                        gpio_intr_disable(ctlr->vsync_pin);
                    }
                }''', 1)
    patched = patched.replace(
        '''            } else {
                ESP_LOGW(TAG, "invalid state %d\\n", ctlr->dvp_fsm);
            }''',
        '''            } else if (!dvp_stream_stop_requested(ctlr)) {
                ESP_LOGW(TAG, "invalid state %d\\n", ctlr->dvp_fsm);
            }''', 1)
    require(patched.endswith('    }\n}\n'), 'Worker terminal boundary drifted')
    patched = patched[:-2] + '''    portENTER_CRITICAL(&ctlr->spinlock);
    ctlr->worker_quiesced = true;
    portEXIT_CRITICAL(&ctlr->spinlock);
    // The owner reads quiesced under the same lock. Do not access ctlr, its
    // queue, callbacks or logs after releasing it; the owner may now free them.
    for (;;) {
        vTaskSuspend(NULL);
    }
}
'''
    source = replace_exact(source, original, patched)
    source = replace_exact(source,
        '    ret = xTaskCreate(dvp_task, DVP_CAM_TASK_NAME, DVP_CAM_TASK_STACK_SIZE, ctlr, DVP_CAM_TASK_PRIORITY, &ctlr->task_handle);',
        '    ret = xTaskCreateWithCaps(dvp_task, DVP_CAM_TASK_NAME, DVP_CAM_TASK_STACK_SIZE, ctlr, DVP_CAM_TASK_PRIORITY, &ctlr->task_handle, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);')
    # Four startup/unwind markers plus twelve staged owner-release markers fit
    # exactly in the existing 24-record video teardown budget.
    require(source.count("rodak_camera_teardown_record(") == 16, "DVP mark budget changed")
    return source


def write_if_changed(path: Path, text: str) -> bool:
    content = text.encode("utf-8")
    if path.exists() and path.read_bytes() == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(content)
    temporary.replace(path)
    return True


def prepare(video_dir: Path, sensor_dir: Path, lock_file: Path, project_manifest: Path,
            idf_path: Path, diagnostics_header: Path, output_dir: Path) -> bool:
    output_dir = output_dir.resolve()
    for protected in (video_dir.resolve().parent, sensor_dir.resolve().parent, idf_path.resolve(),
                      diagnostics_header.resolve().parent):
        require(output_dir != protected and protected not in output_dir.parents,
                "Generated camera overlay must be outside reviewed source directories")
    provenance = json.loads((PATCH_DIR / "provenance.json").read_text(encoding="utf-8"))
    require(lock_graph_digest(lock_file) == provenance["lock_graph_sha256_canonical_json"],
            "Camera dependency graph differs from the reviewed pin (only top-level manifest_hash may vary)")
    require_hash(project_manifest, provenance["project_manifest_sha256_lf"])
    require_hash(diagnostics_header, provenance["diagnostics_header_sha256_lf"])
    reviewed = {}
    for component, root in (("esp_video", video_dir), ("esp_cam_sensor", sensor_dir)):
        entry = provenance["components"][component]
        require((root / ".component_hash").read_text().strip() == entry["component_hash"],
                f"Managed {component} package hash differs")
        reviewed[component] = {name: require_hash(root / name, digest)
                               for name, digest in entry["source_hashes_lf"].items()}
    for name, digest in provenance["idf_source_hashes_lf"].items():
        require_hash(idf_path / name, digest)
    video = instrument_video(reviewed["esp_video"][VIDEO_SOURCE])
    sensor = instrument_sensor(reviewed["esp_cam_sensor"][SENSOR_SOURCE])
    # All validation and exact replacement checks finish before either output changes.
    changed = write_if_changed(output_dir / "esp_video_device_common.c", video)
    return write_if_changed(output_dir / "esp_cam_ctlr_dvp_cam.c", sensor) or changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print-inputs", action="store_true")
    for name in ("video-dir", "sensor-dir", "lock-file", "project-manifest", "idf-path",
                 "diagnostics-header", "output-dir"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    try:
        if args.print_inputs:
            provenance = json.loads((PATCH_DIR / "provenance.json").read_text(encoding="utf-8"))
            inputs = [Path(__file__), PATCH_DIR / "provenance.json", args.lock_file,
                      args.project_manifest, args.diagnostics_header]
            for component, root in (("esp_video", args.video_dir), ("esp_cam_sensor", args.sensor_dir)):
                inputs.append(root / ".component_hash")
                inputs.extend(root / name for name in provenance["components"][component]["source_hashes_lf"])
            inputs.extend(args.idf_path / name for name in provenance["idf_source_hashes_lf"])
            print("\n".join(path.resolve().as_posix() for path in inputs))
            return 0
        changed = prepare(args.video_dir, args.sensor_dir, args.lock_file, args.project_manifest,
                          args.idf_path, args.diagnostics_header, args.output_dir)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"Camera teardown overlay refused: {error}", file=sys.stderr)
        return 1
    print(f"Camera teardown overlay {'generated' if changed else 'verified'}: {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
