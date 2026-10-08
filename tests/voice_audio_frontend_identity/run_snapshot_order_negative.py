"""Require precise failures from the complete frozen 037 frontend's log-first order."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', required=True)
    parser.add_argument('--baseline-source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    suite = Path(__file__).resolve().parent
    root = suite.parents[1]
    baseline = args.baseline_source.read_text()
    normalized_sha = hashlib.sha256(baseline.encode()).hexdigest()
    # d852d9f: all 037 identity/retirement corrections, before the snapshot-order change.
    assert normalized_sha == '022fe2d250c362fe795a0b9ee850bea9af259bbeb520021920046b22592719f8'
    args.output.mkdir(parents=True, exist_ok=False)
    frozen = args.output / 'voice_audio_frontend.cc'
    frozen.write_text(baseline)
    includes = args.output / 'include/phone_os'
    includes.mkdir(parents=True)
    for name in ['voice_audio_frontend.h', 'voice_afe_observation.h']:
        original = subprocess.run(['git', '-C', str(root), 'show', 'd852d9fdb6b16a0a34a49eb7555ddd5a9b944396:main/phone_os/' + name], capture_output=True, check=True).stdout
        assert original.replace(b'\r\n', b'\n') == (root / 'main/phone_os' / name).read_bytes().replace(b'\r\n', b'\n')
        (includes / name).write_bytes(original)
    build = args.output / 'build'
    commands = [
        ['cmake', '-S', str(suite), '-B', str(build), '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Debug', '-DRODAK_FRONTEND_NEGATIVE_CHILD=ON',
         '-DRODAK_FRONTEND_TEST_OBSERVER_INTEGRATION=ON',
         '-DRODAKOS_IDF_PATH=' + args.idf_path,
         '-DRODAK_VOICE_FRONTEND_SOURCE=' + str(frozen),
         '-DRODAK_VOICE_FRONTEND_INCLUDE_ROOT=' + str(includes.parent)],
        ['cmake', '--build', str(build), '--target', 'rodakos_voice_frontend_observer_tests', '-j', '4']]
    for index, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        (args.output / f'build-{index}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError('compile failure is not a detected negative')
    cases = [
        ('open', 'TEST frontend freezes open feed before the first stall tick log blocks',
         'TEST_SNAPSHOT_OPEN producer_progressed=1 open_frozen_before_first_log=0',
         'open_frozen_before_first_log'),
        ('recovered', 'TEST frontend freezes recovered tick before the closure log blocks',
         'TEST_SNAPSHOT_RECOVERED cross_deadline=0 recovered_frozen_before_first_log=0',
         'recovered_frozen_before_first_log'),
        ('deadline', 'TEST frontend retains recovered snapshot when its log crosses the deadline',
         'TEST_SNAPSHOT_RECOVERED cross_deadline=1 recovered_frozen_before_first_log=0',
         'recovered_frozen_before_first_log'),
        ('prefetch_cancel', 'TEST frontend freezes prefetch cancellation before the closure log blocks',
         'TEST_SNAPSHOT_CANCELLED after_fetch=0 cancelled_frozen_before_first_log=0',
         'cancelled_frozen_before_first_log'),
        ('returned_cancel', 'TEST frontend freezes returned cancellation before the closure log blocks',
         'TEST_SNAPSHOT_CANCELLED after_fetch=1 cancelled_frozen_before_first_log=0',
         'cancelled_frozen_before_first_log'),
        ('resync', 'TEST frontend freezes old epoch before the resync closure log blocks',
         'TEST_SNAPSHOT_RESYNC resync_frozen_before_first_log=0 new_epoch_open=1',
         'resync_frozen_before_first_log')]
    results = []
    for name, scenario, marker, assertion in cases:
        result = subprocess.run([str(build / 'rodakos_voice_frontend_observer_tests'), scenario],
                                capture_output=True, text=True, timeout=20)
        combined = result.stdout + result.stderr
        detected = result.returncode == 1 and marker in combined and 'check failed: ' + assertion in combined
        detected = detected and '1 tests, 1 failures' in combined
        record = {'name': name, 'scenario': scenario, 'baselineNormalizedSha256': normalized_sha,
                  'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
                  'detected': detected}
        (args.output / (name + '.json')).write_text(json.dumps(record, indent=2) + '\n')
        results.append(record)
        if not detected:
            raise AssertionError(f'{name}: expected exact ordering assertion absent: {record}')
    (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps({'status': 'PASS_PREVIOUS_ORDER_NEGATIVES', 'detected': len(results)}))


if __name__ == '__main__':
    main()
