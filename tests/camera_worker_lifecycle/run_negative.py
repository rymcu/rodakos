"""Reject targeted real-worker source regressions through specific test assertions."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--idf-path', type=Path, required=True)
    parser.add_argument('--c-flags', default='')
    parser.add_argument('--cxx-flags', default='')
    parser.add_argument('--link-flags', default='')
    args = parser.parse_args()
    original = args.source.read_bytes()
    source = original.decode().replace('\r\n', '\n')
    variants = [
        ('old-delete-held-log', '    dvp_worker_quiesce(ctlr);', '',
         'worker deletion waits for active log', '!(host::DeletedWhileLogHeld())'),
        ('missing-post-receive-check',
         '        BaseType_t received = xQueueReceive(ctlr->event_queue, &event, portMAX_DELAY);\n'
         '        if (dvp_worker_shutdown_requested(ctlr)) {\n            break;\n        }',
         '        BaseType_t received = xQueueReceive(ctlr->event_queue, &event, portMAX_DELAY);',
         'worker shutdown rechecks after a queued event', 'host::CallbackCalls() == 0u'),
        ('wrong-owner-delete-api', '    vTaskDeleteWithCaps(worker);', '    vTaskDelete(worker);',
         'worker constructor uses PSRAM only', 'host::WithCapsDeleteCalls() == 1u'),
        ('internal-stack-regression', '&ctlr->task_handle, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);',
         '&ctlr->task_handle, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);',
         'worker constructor uses PSRAM only',
         'host::CreatedStackCaps() == MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT'),
        ('early-quiesced-publication', '    ctlr->shutdown_requested = true;',
         '    ctlr->shutdown_requested = true;\n    ctlr->worker_quiesced = true;',
         'worker deletion waits for active log', '!(host::DeletedWhileLogHeld())'),
        ('unlocked-quiesced-read',
         '        portENTER_CRITICAL(&ctlr->spinlock);\n        bool quiesced = ctlr->worker_quiesced;\n'
         '        portEXIT_CRITICAL(&ctlr->spinlock);',
         '        bool quiesced = ctlr->worker_quiesced;',
         'worker final access confirmation', 'retained'),
        ('missing-stream-stop-check',
         '''static bool dvp_stream_stop_requested(dvp_cam_ctlr_t *ctlr)
{
    portENTER_CRITICAL(&ctlr->spinlock);
    bool requested = ctlr->stream_stop_requested;
    portEXIT_CRITICAL(&ctlr->spinlock);
    return requested;
}''',
         '''static bool dvp_stream_stop_requested(dvp_cam_ctlr_t *ctlr)
{
    (void)ctlr;
    return false;
}''',
         'stream stop drops an admitted partial frame without restart or error',
         'host::InvalidStateLogs() == 0u')]
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for name, before, after, test_filter, expected in variants:
        assert source.count(before) == 1, name + ': mutation boundary drift'
        directory = args.output / name
        directory.mkdir(parents=True, exist_ok=True)
        mutated = directory / 'esp_cam_ctlr_dvp_cam.c'
        mutated_source = source.replace(before, after, 1)
        if name == 'old-delete-held-log':
            mutated_source = mutated_source.replace('    vTaskDeleteWithCaps(worker);', '    vTaskDelete(worker);', 1)
        mutated.write_text(mutated_source, newline='\n')
        build = directory / 'build'
        commands = [
            ['cmake', '-S', str(ROOT / 'tests/camera_worker_lifecycle'), '-B', str(build),
             '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug', '-DPython3_EXECUTABLE=' + sys.executable,
             '-DRODAKOS_IDF_PATH=' + str(args.idf_path), '-DRODAK_WORKER_SOURCE=' + str(mutated),
             '-DCMAKE_C_FLAGS=' + args.c_flags, '-DCMAKE_CXX_FLAGS=' + args.cxx_flags,
             '-DCMAKE_EXE_LINKER_FLAGS=' + args.link_flags],
            ['cmake', '--build', str(build), '-j', '2']]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            (directory / f'build-{index}.log').write_text(result.stdout + result.stderr)
            assert result.returncode == 0, f'{name}: build failure is not a detected regression'
        result = subprocess.run([str(build / 'rodakos_camera_worker_lifecycle_tests'), '--filter', test_filter],
                                capture_output=True, text=True, timeout=15)
        output = result.stdout + result.stderr
        (directory / 'result.log').write_text(output)
        assert result.returncode == 1 and '1 tests, 1 failures' in output and expected in output, output
        assert not re.search(r'AddressSanitizer|LeakSanitizer|runtime error:', output), output
        results.append({'variant': name, 'detected': True, 'exitCode': result.returncode,
                        'testFilter': test_filter, 'expectedAssertion': expected,
                        'sourceSha256': hashlib.sha256(mutated.read_bytes()).hexdigest()})
        print('Detected ' + name, flush=True)
    assert args.source.read_bytes() == original
    (args.output / 'negative-results.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__': main()
