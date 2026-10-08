"""Require real assistant TU regressions; never accept a build failure or timeout as detection."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SHA = lambda raw: hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline-root', type=Path)
    parser.add_argument('--only', help='Run one identified variant or original-self-delete')
    parser.add_argument('--idf-path', default=os.environ.get('RODAKOS_IDF_PATH', os.environ.get('IDF_PATH', '')))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source_path = ROOT / 'main/phone_os/voice_assistant_service.cc'
    header_path = source_path.with_suffix('.h')
    original = source_path.read_bytes()
    header = header_path.read_bytes()
    source = original.decode().replace('\r\n', '\n')
    records = []

    def run(name, candidate, include, test_filter, assertion=None, baseline=False):
        folder = args.output / name
        folder.mkdir(exist_ok=True)
        build = folder / 'build'
        options = ['-DVOICE_ASSISTANT_SOURCE=' + str(candidate), '-DVOICE_ASSISTANT_BASELINE_INCLUDE=' + str(include or '')]
        if baseline:
            options.append('-DRODAK_ASSISTANT_LEGACY_BASELINE=ON')
        configure = ['cmake', '-S', str(ROOT / 'tests/voice_volume_service'), '-B', str(build), '-G', 'Ninja',
                     '-DCMAKE_BUILD_TYPE=Debug', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DRODAKOS_IDF_PATH=' + args.idf_path, *options]
        for index, command in enumerate([configure, ['cmake', '--build', str(build), '-j', '2']]):
            result = subprocess.run(command, capture_output=True, timeout=180)
            (folder / f'build-{index}.log').write_bytes(result.stdout + result.stderr)
            if result.returncode != 0:
                raise RuntimeError(name + ': build failure is not regression detection')
        command = [str(build / 'rodakos_voice_volume_service_tests'), '--filter', test_filter]
        result = subprocess.run(command, capture_output=True, timeout=20)
        raw = result.stdout + result.stderr
        (folder / 'result.log').write_bytes(raw)
        output = raw.decode('utf-8', errors='replace')
        if baseline:
            expected = ['ASSISTANT_IO_STARTED_REAL_SERVICE',
                        'EXPECTED_IDF_CLEANUP_CREATE_REJECTED name=prvTaskDeleteWithCapsTask',
                        'Failed to create the task to delete the current task']
            detected = result.returncode == -signal.SIGABRT and all(marker in output for marker in expected)
        else:
            expected = ['1 tests, 1 failures', 'check failed: ' + assertion]
            detected = result.returncode == 1 and all(marker in output for marker in expected)
        if not detected or re.search(r'AddressSanitizer|LeakSanitizer|runtime error:', output):
            raise RuntimeError(name + ': intended failure not identified\n' + output)
        records.append({'name': name, 'kind': 'complete-unchanged-baseline-IDF-abort' if baseline else 'complete-TU-mutation',
                        'detected': True, 'command': command, 'expected': expected, 'exitCode': result.returncode,
                        'sourceSha256': SHA(candidate.read_bytes()), 'resultSha256': SHA(raw)})
        print('Detected ' + name, flush=True)

    variants = [
        ('missing-exact-join', '    retirement.Join();\n    reconnect_coordinator_.Cancel',
         '    // Negative control: wait only for business cleanup.\n    reconnect_coordinator_.Cancel',
         'retirement assistant late idle Stop', '!(returned_early)'),
        ('lost-previous-ticket', '        io_retirement_ = previous_retirement;',
         '        io_retirement_.Reset();',
         'retirement assistant failed replacement preserves', '!(returned_early)'),
        ('cleanup-follows-replacement', 'const bool complete = !stopping_ || cleanup_generation_ != generation;',
         'const bool complete = !stopping_;',
         'retirement assistant cleanup waiter', 'old_only'),
        ('deinit-follows-replacement', 'const bool complete = !deinitializing_ ||\n                                  deinitialization_generation_ != pending_generation;',
         'const bool complete = !deinitializing_;',
         'retirement assistant Deinit waiter', 'old_only'),
        ('destructor-admission-open', '    io_retirement_owner_.Close();',
         '    // Negative control: destructor leaves admission open.',
         'retirement assistant destructor closes', '!(accepted.load())'),
        ('stopping-joins-latest', '            retirement.Join();',
         '            xSemaphoreTake(mutex_, portMAX_DELAY);\n            retirement = io_retirement_;\n            xSemaphoreGive(mutex_);\n            retirement.Join();',
         'retirement assistant stopping waiter', 'old_only')
    ]
    for name, before, after, test_filter, assertion in variants:
        if args.only and args.only != name:
            continue
        if source.count(before) != 1:
            raise RuntimeError(name + ': mutation boundary drifted')
        folder = args.output / name
        folder.mkdir()
        candidate = folder / 'voice_assistant_service.cc'
        candidate.write_text(source.replace(before, after, 1), encoding='utf-8', newline='\n')
        run(name, candidate, None, test_filter, assertion)

    if args.baseline_root and (not args.only or args.only == 'original-self-delete'):
        baseline = args.baseline_root.resolve()
        manifest = json.loads((baseline / 'manifest.json').read_bytes())
        for relative, item in manifest['files'].items():
            raw = (baseline / relative.replace('\\', '/')).read_bytes()
            if SHA(raw) != item['sha256'] or len(raw) != item['bytes']:
                raise RuntimeError('Baseline drifted: ' + relative)
        run('original-self-delete', baseline / 'phone_os/voice_assistant_service.cc', baseline,
            'retirement assistant exits without allocating', baseline=True)
        records[-1]['baselineCommit'] = manifest['sourceCommit']
        records[-1]['headerSha256'] = SHA((baseline / 'phone_os/voice_assistant_service.h').read_bytes())
    if source_path.read_bytes() != original or header_path.read_bytes() != header:
        raise RuntimeError('Production source changed during controls')
    if not records:
        raise RuntimeError('No negative control selected')
    (args.output / 'negative-results.json').write_text(json.dumps({'sourceSha256': SHA(original),
        'headerSha256': SHA(header), 'records': records}, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
