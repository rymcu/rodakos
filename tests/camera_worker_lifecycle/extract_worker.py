"""Extract complete pinned C worker, constructor and destructor; hardware/RTOS remain host fakes."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('camera_overlay', ROOT / 'tools/prepare_camera_teardown_patch.py')
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


def function(source, name):
    match = re.search(r'(?m)^(?:static )?(?:IRAM_ATTR )?[\w*]+\s+' + re.escape(name) + r'\([^;]*?\)\n\{', source)
    if not match:
        raise ValueError('Missing complete production function: ' + name)
    opening = source.index('{', match.start())
    depth = 0
    for end in range(opening, len(source)):
        if source[end] == '{': depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0: return source[match.start():end + 1]
    raise ValueError('Unclosed function: ' + name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--source', default='')
    args = parser.parse_args()
    overlay.prepare(ROOT / 'managed_components/espressif__esp_video',
                    ROOT / 'managed_components/espressif__esp_cam_sensor', ROOT / 'dependencies.lock',
                    ROOT / 'main/idf_component.yml', args.idf_path,
                    ROOT / 'main/phone_os/camera-teardown-diagnostics.h', args.output_dir)
    path = Path(args.source) if args.source else args.output_dir / 'esp_cam_ctlr_dvp_cam.c'
    source = path.read_text().replace('\r\n', '\n')
    types = source[source.index('typedef enum dvp_cam_event_type'):source.index('static const char *TAG')]
    overlay.write_if_changed(args.output_dir / 'production_types.h', types)
    names = ['dvp_get_dma_buffer_hsize', 'dvp_dma_deinit', 'dvp_stop_capturing']
    if 'static bool dvp_worker_shutdown_requested' in source:
        names += ['dvp_worker_shutdown_requested', 'dvp_worker_quiesce']
    names += ['dvp_cam_ctlr_del', 'dvp_cam_ctlr_register_event_callbacks', 'dvp_cam_ctlr_start',
              'dvp_cam_ctlr_stop', 'dvp_cam_ctlr_enable', 'dvp_cam_ctlr_disable',
              'dvp_task', 'esp_cam_new_dvp_ctlr_ext']
    functions = '\n\n'.join(function(source, name) for name in names)
    wrappers = '''
esp_err_t worker_create(esp_cam_ctlr_handle_t *out) {
    esp_cam_ctlr_dvp_config_t config = {.ctlr_id=0, .pin_dont_init=true, .external_xtal=true};
    return esp_cam_new_dvp_ctlr_ext(&config, out);
}
esp_err_t worker_delete(esp_cam_ctlr_handle_t handle) { return dvp_cam_ctlr_del(handle); }
'''
    marker = '#define RODAK_WORKER_COOPERATIVE 1\n' if 'bool shutdown_requested;' in source else ''
    overlay.write_if_changed(args.output_dir / 'production_mode.h', marker)
    compiled = '#include "worker_fakes.h"\nstatic const char *TAG = "dvp_ext";\nstatic int s_vsync_io = 7;\n' + functions + wrappers
    overlay.write_if_changed(args.output_dir / 'production_worker.c', compiled)
    report = {'source': str(path), 'sourceSha256Lf': hashlib.sha256(source.encode()).hexdigest(),
              'compiledFunctions': names, 'compiledSha256Lf': hashlib.sha256(compiled.encode()).hexdigest(),
              'mode': 'complete-production-C-functions-with-real-controller-type-and-host-dependencies'}
    overlay.write_if_changed(args.output_dir.parent / 'production-sources.json', json.dumps(report, indent=2) + '\n')


if __name__ == '__main__': main()
