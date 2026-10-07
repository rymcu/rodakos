"""Freeze a complete pre-retirement wake TU/header; require the real IDF failure."""
import argparse
import hashlib
import json
import signal
import subprocess
from pathlib import Path


BASELINE = 'cbb6c9872e86ece51bbd2fefb45d75ca156bac35'
SCENARIO = 'wake retirement reclaims the complete supervisor without allocating on Stop'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    suite = Path(__file__).resolve().parent
    root = suite.parents[1]
    args.output.mkdir(parents=True, exist_ok=True)
    include = args.output / 'include'
    (include / 'phone_os').mkdir(parents=True, exist_ok=True)
    fingerprints = {}
    for suffix in ('cc', 'h'):
        path = f'main/phone_os/voice_wake_service.{suffix}'
        content = subprocess.run(['git', '-C', str(root), 'show', f'{BASELINE}:{path}'],
                                 check=True, capture_output=True).stdout
        destination = (args.output if suffix == 'cc' else include / 'phone_os') / f'voice_wake_service.{suffix}'
        destination.write_bytes(content)
        fingerprints[path] = hashlib.sha256(content).hexdigest()
    build = args.output / 'build'
    commands = [
        ['cmake', '-S', str(suite), '-B', str(build), '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Debug', '-DRODAK_WAKE_NEGATIVE_CHILD=ON',
         f'-DRODAKOS_IDF_PATH={args.idf_path}',
         f'-DVOICE_WAKE_SOURCE={args.output / "voice_wake_service.cc"}',
         f'-DVOICE_WAKE_BASELINE_INCLUDE={include}'],
        ['cmake', '--build', str(build), '-j', '4']]
    for index, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        (args.output / f'build-{index}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError('Old-source compile failure is not a negative pass')
    result = subprocess.run([str(build / 'rodakos_voice_wake_service_tests'), SCENARIO],
                            capture_output=True, text=True, timeout=10)
    report = {'baseline': BASELINE, 'sources': fingerprints, 'scenario': SCENARIO,
              'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
    (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    combined = result.stdout + result.stderr
    expected = ['EXPECTED_IDF_CLEANUP_CREATE_REJECTED name=prvTaskDeleteWithCapsTask',
                'Failed to create the task to delete the current task']
    if result.returncode != -signal.SIGABRT or any(marker not in combined for marker in expected):
        raise AssertionError('Expected named real IDF cleanup failure and SIGABRT, not timeout/teardown')
    if any(marker in combined for marker in ['terminate called', 'RETIREMENT_HOST_ASSERT', 'check failed:']):
        raise AssertionError('Unrelated harness/test teardown must not satisfy the negative control')


if __name__ == '__main__':
    main()
