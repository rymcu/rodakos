"""Require identified full service translation units to expose retirement regressions."""
from __future__ import annotations

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
    parser.add_argument('--suite', choices=('display', 'peers'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline-root', type=Path)
    parser.add_argument('--baseline-only', action='store_true')
    parser.add_argument('--idf-path', default=os.environ.get('RODAKOS_IDF_PATH', os.environ.get('IDF_PATH', '')))
    parser.add_argument('--c-flags', default='')
    parser.add_argument('--cxx-flags', default='')
    parser.add_argument('--link-flags', default='')
    args = parser.parse_args()
    if args.baseline_only and not args.baseline_root:
        parser.error('--baseline-only requires --baseline-root')
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    sources = ['display_service'] if args.suite == 'display' else ['webrtc_camera_service', 'webrtc_display_service']
    suite = 'display_service' if args.suite == 'display' else 'display_control_ack_service'
    executable = 'rodakos_' + suite + '_tests'
    original_files = {}
    for name in sources:
        for suffix in ('cc', 'h'):
            path = ROOT / 'main/phone_os' / f'{name}.{suffix}'
            original_files[path] = path.read_bytes()

    def execute(name, overrides, test_filter, assertion=None, baseline=False):
        directory = args.output / name
        directory.mkdir(parents=True, exist_ok=True)
        build = directory / 'build'
        command = ['cmake', '-S', str(ROOT / 'tests' / suite), '-B', str(build), '-G', 'Ninja',
                   '-DCMAKE_BUILD_TYPE=Debug', '-DRODAKOS_IDF_PATH=' + args.idf_path,
                   '-DCMAKE_C_FLAGS=' + args.c_flags, '-DCMAKE_CXX_FLAGS=' + args.cxx_flags,
                   '-DCMAKE_EXE_LINKER_FLAGS=' + args.link_flags, *overrides]
        for index, invocation in enumerate((command, ['cmake', '--build', str(build), '-j', '2'])):
            result = subprocess.run(invocation, capture_output=True, timeout=180)
            (directory / f'build-{index}.log').write_bytes(result.stdout + result.stderr)
            if result.returncode != 0:
                raise RuntimeError(f'{name}: build failure is not a detected retirement regression')
        result = subprocess.run([str(build / executable), '--filter', test_filter],
                                capture_output=True, timeout=20)
        raw = result.stdout + result.stderr
        output = raw.decode('utf-8', errors='replace')
        (directory / 'result.log').write_bytes(raw)
        clean = not re.search(r'AddressSanitizer|LeakSanitizer|runtime error:', output)
        if baseline:
            expected = ['EXPECTED_IDF_CLEANUP_CREATE_REJECTED name=prvTaskDeleteWithCapsTask',
                        'Failed to create the task to delete the current task']
            detected = result.returncode == -signal.SIGABRT and all(marker in output for marker in expected)
        else:
            expected = ['1 tests, 1 failures', 'check failed: ' + assertion]
            detected = result.returncode == 1 and all(marker in output for marker in expected)
        if not detected or not clean:
            raise RuntimeError(f'{name}: missed intended retirement failure\n{output}')
        record = {'name': name, 'kind': 'unchanged-old-source-IDF-abort' if baseline else 'full-TU-mutation',
                  'detected': True, 'filter': test_filter, 'expected': expected,
                  'exitCode': result.returncode, 'resultSha256': SHA(raw), 'overrides': overrides}
        records.append(record)
        print('Detected ' + name, flush=True)

    for name in ([] if args.baseline_only else sources):
        path = ROOT / 'main/phone_os' / f'{name}.cc'
        source = original_files[path].decode().replace('\r\n', '\n')
        peer = name.startswith('webrtc_')
        kind = 'camera' if name == 'webrtc_camera_service' else 'display'
        option = 'RODAK_' + kind.upper() + '_NEGATIVE_SOURCE' if peer else 'DISPLAY_SERVICE_SOURCE'
        join = '        retirement.Join();' if peer else '    retirement.Join();'
        if source.count(join) != 1:
            raise RuntimeError('Join mutation boundary drifted: ' + name)
        late_filter = ('retirement peer late Stop waits' if peer else
                       'retirement waits for late callback destruction')
        cross_filter = ('retirement peer Stop joins old generation' if peer else
                        'Stop waits only for its old generation')
        lock = ('std::lock_guard<std::recursive_mutex> lock(mutex_); retirement = peer_retirement_;'
                if peer else 'SemaphoreLock lock(mutex_); retirement = jpeg_stream_retirement_;')
        variants = [('no-exact-join', join, '// Negative control: native completion is incorrectly treated as join.',
                     late_filter, '!(returned_early)'),
                    ('join-replacement', join, join + '\n    { ' + lock + ' }\n    retirement.Join();',
                     cross_filter, 'joined_old_only' if peer else 'old_stop_completed')]
        for variant, before, after, test_filter, assertion in variants:
            directory = args.output / (name + '-' + variant)
            directory.mkdir(parents=True, exist_ok=True)
            mutated = directory / (name + '.cc')
            mutated.write_text(source.replace(before, after, 1), encoding='utf-8', newline='\n')
            execute(directory.name, ['-D' + option + '=' + str(mutated.resolve())], test_filter, assertion)
            records[-1].update({'originalRawSha256': SHA(original_files[path]),
                                'mutatedSha256': SHA(mutated.read_bytes()),
                                'headerSha256': SHA(original_files[path.with_suffix('.h')])})

    if args.baseline_root:
        baseline = args.baseline_root.resolve()
        manifest = json.loads((baseline / 'manifest.json').read_bytes())
        for relative, entry in manifest['files'].items():
            raw = (baseline / relative).read_bytes()
            if SHA(raw) != entry['sha256'] or len(raw) != entry['bytes']:
                raise RuntimeError('Old-source baseline drifted: ' + relative)
        for name in sources:
            peer = name.startswith('webrtc_')
            kind = 'camera' if name == 'webrtc_camera_service' else 'display'
            header_root = baseline / ('peers' if peer else 'display')
            overrides = ['-DRODAK_SERVICE_BASELINE_INCLUDE=' + str(header_root)]
            if peer:
                overrides += ['-DRODAK_CAMERA_NEGATIVE_SOURCE=' + str(header_root / 'phone_os/webrtc_camera_service.cc'),
                              '-DRODAK_DISPLAY_NEGATIVE_SOURCE=' + str(header_root / 'phone_os/webrtc_display_service.cc')]
                test_filter = 'retirement ' + kind + ' peer exits without allocating'
            else:
                overrides += ['-DDISPLAY_SERVICE_SOURCE=' + str(header_root / 'phone_os/display_service.cc')]
                test_filter = 'retirement display exits without allocating'
            execute(name + '-original-self-delete', overrides, test_filter, baseline=True)
            relative = str((header_root / ('phone_os/' + name + '.cc')).relative_to(baseline))
            records[-1].update({'baselineCommit': manifest['sourceCommit'],
                               'baselineSourceSha256': manifest['files'][relative]['sha256']})
    for path, raw in original_files.items():
        if path.read_bytes() != raw:
            raise RuntimeError('Production source changed during controls: ' + str(path))
    (args.output / 'negative-results.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    print(f'{len(records)} full service retirement controls detected', flush=True)


if __name__ == '__main__':
    main()
