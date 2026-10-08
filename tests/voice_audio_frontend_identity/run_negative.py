"""Require the old complete TU to abort in real IDF and detect skipped vector destruction."""
import argparse
import hashlib
import json
import signal
import subprocess
from pathlib import Path


SCENARIO = 'Capture retirement frees the actual AFE vector before external task delete'
BASELINE = '34c9e6453344b8eb7896ab5476ef222504c12a94'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--case', choices=['legacy_self_delete', 'skipped_vector_destructor'])
    args = parser.parse_args()
    suite = Path(__file__).resolve().parent
    root = suite.parents[1]
    args.output.mkdir(parents=True, exist_ok=True)
    source = root / 'main/phone_os/voice_audio_frontend.cc'
    original = source.read_text()
    baseline = subprocess.run(
        ['git', '-C', str(root), 'show', f'{BASELINE}:main/phone_os/voice_audio_frontend.cc'],
        check=True, capture_output=True, text=True).stdout
    baseline_header = subprocess.run(
        ['git', '-C', str(root), 'show', f'{BASELINE}:main/phone_os/voice_audio_frontend.h'],
        check=True, capture_output=True, text=True).stdout
    needle = 'std::vector<int16_t> afe_feed_buffer;'
    if original.count(needle) != 1:
        raise AssertionError('Vector negative control needs one exact production allocation')
    cases = {
        'legacy_self_delete': baseline,
        'skipped_vector_destructor': original.replace(
            needle, 'auto& afe_feed_buffer = *new std::vector<int16_t>;'),
    }
    reports = []
    for name, content in cases.items():
        if args.case and name != args.case:
            previous = args.output / name / 'result.json'
            if previous.exists():
                reports.append(json.loads(previous.read_text()))
            continue
        directory = args.output / name
        directory.mkdir(parents=True, exist_ok=True)
        fixture_source = directory / 'voice_audio_frontend.cc'
        fixture_source.write_text(content)
        include_root = directory / 'include'
        header = include_root / 'phone_os/voice_audio_frontend.h'
        header.parent.mkdir(parents=True, exist_ok=True)
        header_content = baseline_header if name == 'legacy_self_delete' else (
            root / 'main/phone_os/voice_audio_frontend.h').read_text()
        header.write_text(header_content)
        build = directory / 'build'
        commands = [
            ['cmake', '-S', str(suite), '-B', str(build), '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Debug', '-DRODAK_FRONTEND_NEGATIVE_CHILD=ON',
             '-DRODAK_FRONTEND_OBSERVATION_TESTS=OFF',
             f'-DRODAKOS_IDF_PATH={args.idf_path}',
             f'-DRODAK_VOICE_FRONTEND_SOURCE={fixture_source}',
             f'-DRODAK_VOICE_FRONTEND_INCLUDE_ROOT={include_root}',
             '-DRODAK_FRONTEND_LEGACY_PROBE=' + ('ON' if name == 'legacy_self_delete' else 'OFF')],
            ['cmake', '--build', str(build), '-j', '4'],
        ]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            (directory / f'build-{index}.log').write_text(result.stdout + result.stderr)
            if result.returncode:
                raise AssertionError(f'{name}: build failure cannot count as a negative pass')
        result = subprocess.run(
            [str(build / 'rodakos_voice_audio_frontend_identity_tests'), SCENARIO],
            capture_output=True, text=True, timeout=12)
        combined = result.stdout + result.stderr
        report = {'name': name, 'sourceSha256': hashlib.sha256(content.encode()).hexdigest(),
                  'headerSha256': hashlib.sha256(header_content.encode()).hexdigest(),
                  'baseline': BASELINE if name == 'legacy_self_delete' else None,
                  'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        reports.append(report)
        if 'CAPTURE_AFE_FEED_OBSERVED' not in combined:
            raise AssertionError(f'{name}: no real CaptureTask AFE feed observed')
        if name == 'legacy_self_delete':
            expected = ['EXPECTED_IDF_CLEANUP_CREATE_REJECTED name=prvTaskDeleteWithCapsTask',
                        'Failed to create the task to delete the current task']
            if result.returncode != -signal.SIGABRT or any(x not in combined for x in expected):
                raise AssertionError(f'{name}: expected explicit real IDF cleanup failure and SIGABRT')
            if 'CAPTURE_AFE_BUFFER_RELEASED' in combined:
                raise AssertionError('Legacy task deletion incorrectly unwound production vector')
            if any(marker in combined for marker in
                   ['terminate called', 'RETIREMENT_HOST_ASSERT', 'check failed:']):
                raise AssertionError('Legacy failure raced into unrelated harness/test teardown')
        elif result.returncode != 1 or 'buffer_released_before_delete.load()' not in combined:
            raise AssertionError('Skipped vector destructor did not trigger the precise lifecycle check')
    (args.output / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')


if __name__ == '__main__':
    main()
