"""Mutate full production retirement TUs and require scenario-specific aborts."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CASES = [
    ('publish_handshake', 'publication',
     'if (record->state == State::kPublished) {',
     'if (record->state == State::kPublished || record->state == State::kReserved) {',
     'RETIREMENT_TEST_ASSERT: body must wait for handle publication'),
    ('finished_before_destructors', 'destructor',
     '    body(argument);',
     '    { RegistryLock lock; record->state = State::kFinished; }\n    body(argument);',
     'RETIREMENT_TEST_ASSERT: complete body destruction precedes retirement claim'),
    ('duplicate_claim', 'concurrent',
     '        record.state = State::kReaping;',
     '        record.state = State::kFinished;',
     'RETIREMENT_HOST_ASSERT: duplicate external retirement claim'),
    ('missing_autonomous_pump', 'autonomous',
     '        if (generation != 0) TryReap(slot, generation, current);',
     '        (void)generation; (void)current;',
     'RETIREMENT_TEST_ASSERT: autonomous worker reclaimed by permanent pump'),
    ('reuse_with_live_reference', 'capacity',
     '    if (record.references == 0 && record.state == State::kReaped) record = {};',
     '    if (record.state == State::kReaped) record = {};',
     'RETIREMENT_TEST_ASSERT: retained ticket prevents ABA reuse'),
    ('closed_owner_admits', 'drain',
     '    closed_ = true;',
     '    closed_ = false;',
     'RETIREMENT_TEST_ASSERT: closed owner rejects new generation'),
]


def checked(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    if result.returncode:
        raise AssertionError(f'Build/setup is not a negative pass: {command}\n{result.stdout}{result.stderr}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source_path = ROOT / 'main/phone_os/task-retirement.cc'
    source = source_path.read_text()
    args.output.mkdir(parents=True, exist_ok=True)
    reports = []
    for name, scenario, original, replacement, marker in CASES:
        if source.count(original) != 1:
            raise AssertionError(f'{name}: source changed; exact mutation must be reviewed')
        folder = args.output / name
        folder.mkdir(exist_ok=True)
        mutant = folder / 'task-retirement.cc'
        mutant.write_text(source.replace(original, replacement), newline='\n')
        build = folder / 'build'
        checked(['cmake', '-S', str(Path(__file__).parent), '-B', str(build),
                 '-DRODAKOS_IDF_PATH=' + args.idf_path,
                 '-DRODAK_RETIREMENT_SOURCE=' + str(mutant),
                 '-DRODAK_TASK_RETIREMENT_BUILD_TESTS=ON',
                 '-DRODAK_RETIREMENT_NEGATIVE_CHILD=ON'])
        checked(['cmake', '--build', str(build), '-j', '2'])
        result_file = folder / 'result.json'
        checked([sys.executable, str(Path(__file__).parent / 'assert_abort.py'),
                 '--executable', str(build / 'rodakos_task_retirement_tests'),
                 '--scenario', scenario, '--expected', marker, '--output', str(result_file)])
        reports.append({'name': name, 'scenario': scenario, 'expected': marker,
                        'mutantSha256': hashlib.sha256(mutant.read_bytes()).hexdigest(),
                        'result': json.loads(result_file.read_text())})
    manifest = {'source': str(source_path),
                'sourceRawSha256': hashlib.sha256(source_path.read_bytes()).hexdigest(),
                'negativeControls': reports}
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'{len(reports)} complete-TU negative controls rejected by exact expected assertions')


if __name__ == '__main__':
    main()
