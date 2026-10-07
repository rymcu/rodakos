"""Compile the complete reviewed WithCaps function chain; replace only dependencies."""
import argparse
import hashlib
import json
from pathlib import Path

PIN = 'a89370f78711e8051b530603c08c02fd8675e090e1598e58f3d94fbdc24f56d4'
FUNCTIONS = ['xTaskCreatePinnedToCoreWithCaps', 'prvTaskDeleteWithCaps',
             'prvTaskDeleteWithCapsTask', 'vTaskDeleteWithCaps']
SUPPORT_PINS = {
    'components/freertos/esp_additions/include/freertos/idf_additions.h':
        'e59ae9cea85fa3a3df6384bfcd7238a100212c8fb626dc58d23f1710e8441a40',
    'components/freertos/esp_additions/freertos_tasks_c_additions.h':
        'b4f3e49d4521586eb7ecfaedb2dc0d02126662a069b40dec0a4de520e65fbef6',
    'components/freertos/heap_idf.c':
        '6201b70a245f721c3021656d62fbbbf45c3e4d5d843365df007d0da9ac99db2e',
    'components/freertos/config/include/freertos/FreeRTOSConfig.h':
        '643e47abb160200195e182e79e891f49d693a60b209e403a7359c66a2bcac0e5',
    'components/freertos/config/xtensa/include/freertos/FreeRTOSConfig_arch.h':
        'cd08bdaa24178129f9ef1504bc932d30415c89eb5b16ac6f00a8367f39f61041',
    'components/freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos/portmacro.h':
        '7a62b39c80f119d2c06dc7eee4dae4d4b4c68581f832b8b8b052345b5d6d2366',
}


def prepare(idf_path, output, retirement_source=None):
    source = idf_path / 'components/freertos/esp_additions/idf_additions.c'
    raw = source.read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != PIN:
        raise ValueError(f'Refusing unreviewed IDF WithCaps: {actual}, expected {PIN}')
    support = []
    for relative, expected in SUPPORT_PINS.items():
        digest = hashlib.sha256((idf_path / relative).read_bytes()).hexdigest()
        if digest != expected:
            raise ValueError(f'Refusing unreviewed support input {relative}: {digest}')
        support.append({'path': relative, 'rawSha256': digest, 'compiled': False})
    config_path = Path(__file__).resolve().parents[2] / 'sdkconfig'
    config_raw = config_path.read_bytes()
    config_lines = config_raw.decode().splitlines()
    required = ['# CONFIG_FREERTOS_SMP is not set', '# CONFIG_FREERTOS_UNICORE is not set',
                'CONFIG_FREERTOS_IDLE_TASK_STACKSIZE=1536', 'CONFIG_FREERTOS_NUMBER_OF_CORES=2',
                'CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y',
                '# CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK is not set',
                'CONFIG_COMPILER_STACK_CHECK_MODE_NONE=y',
                '# CONFIG_COMPILER_OPTIMIZATION_NONE is not set',
                '# CONFIG_ESP_TRACE_TRANSPORT_APPTRACE is not set',
                '# CONFIG_STACK_CHECK_ALL is not set', '# CONFIG_STACK_CHECK_STRONG is not set']
    for setting in required:
        if setting not in config_lines:
            raise ValueError('Target configuration changed; review host assumptions: ' + setting)
    text = raw.decode().replace('\r\n', '\n')
    start = text.index('/* -------------------------------------------- Creation With Memory Caps')
    end = text.index('/* ---------------------------------- Queue')
    # Preserve the complete task section, including both private helpers, comments,
    # error paths and preprocessor guards. No function body is patched.
    compiled = ('#include <stdlib.h>\n#include "freertos/task.h"\n'
                '#include "esp_heap_caps.h"\n#include "esp_log.h"\n' + text[start:end])
    output.mkdir(parents=True, exist_ok=True)
    (output / 'idf_withcaps.c').write_text(compiled, newline='\n')
    fixture_root = Path(__file__).resolve().parent
    project_root = fixture_root.parents[1]
    production_source = retirement_source or project_root / 'main/phone_os/task-retirement.cc'
    inputs = [production_source, project_root / 'main/phone_os/task-retirement.h',
              fixture_root / 'host_runtime.cc', fixture_root / 'task_retirement_host.h',
              fixture_root / 'prepare_idf.py', fixture_root / 'retirement_test.cc',
              fixture_root / 'CMakeLists.txt', fixture_root / 'assert_abort.py',
              fixture_root / 'run_negative.py']
    inputs += sorted((fixture_root / 'fakes').rglob('*.h'))
    inputs += sorted((fixture_root / 'private_fakes').rglob('*.h'))
    report = {'source': str(source), 'sourceRawSha256': actual,
              'sourceLfSha256': hashlib.sha256(text.encode()).hexdigest(),
              'compiledSha256': hashlib.sha256(compiled.encode()).hexdigest(),
              'compiledFunctions': FUNCTIONS,
              'reviewedSupportInputs': support,
              'targetConfiguration': {'path': str(config_path),
                                      'rawSha256': hashlib.sha256(config_raw).hexdigest(),
                                      'requiredSettings': required},
              'hostAbi': {'StaticTaskStorageBytes': 100, 'StackTypeBytes': 1,
                          'cores': 2, 'minimalStackBytes': 1536,
                          'scope': 'Explicit fixture assumptions; not a new target ABI measurement'},
              'compiledAndHarnessInputs': [
                  {'path': str(path), 'rawSha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                  for path in inputs],
              'allocatorBoundary': 'compile-time names mapped to retirement_host_heap_*',
              'scope': 'Real IDF WithCaps functions; scheduler, core queries and allocator are host fakes'}
    (output / 'provenance.json').write_text(json.dumps(report, indent=2) + '\n', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--retirement-source', type=Path)
    args = parser.parse_args()
    prepare(args.idf_path, args.output_dir, args.retirement_source)
