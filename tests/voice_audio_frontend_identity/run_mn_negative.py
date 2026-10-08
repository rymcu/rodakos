"""旧完整 TU + 配套头必须命中真实 SDK registry 的重复分配和重复释放。"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


BASELINE = 'e6edadcd8d72ea8300c8afe3b0ccc1bb359eb80c'
SCENARIOS = [
    ('MultiNet create owns the single real SDK registry allocation',
     'MN_ALLOCATION_OBSERVED allocations=2 reallocations=1',
     'check failed: stats.allocations == 1U'),
    ('MultiNet destroy frees the real SDK registry exactly once',
     'MN_DESTRUCTION_OBSERVED frees=2 empty_frees=1',
     'check failed: stats.frees == 1U'),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    suite = Path(__file__).resolve().parent
    root = suite.parents[1]
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.output / 'voice_audio_frontend.cc'
    includes = args.output / 'include'
    header = includes / 'phone_os/voice_audio_frontend.h'
    header.parent.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for relative, target in [
        ('main/phone_os/voice_audio_frontend.cc', source),
        ('main/phone_os/voice_audio_frontend.h', header),
    ]:
        content = subprocess.run(
            ['git', '-C', str(root), 'show', f'{BASELINE}:{relative}'],
            check=True, capture_output=True).stdout
        target.write_bytes(content)
        hashes[relative] = hashlib.sha256(content).hexdigest()
    build = args.output / 'build'
    commands = [
        ['cmake', '-S', str(suite), '-B', str(build), '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Debug', '-DRODAK_FRONTEND_NEGATIVE_CHILD=ON',
         '-DRODAK_FRONTEND_OBSERVATION_TESTS=OFF',
         f'-DRODAKOS_IDF_PATH={args.idf_path}',
         f'-DRODAK_VOICE_FRONTEND_SOURCE={source}',
         f'-DRODAK_VOICE_FRONTEND_INCLUDE_ROOT={includes}'],
        ['cmake', '--build', str(build), '-j', '4'],
    ]
    for index, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        (args.output / f'build-{index}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError('Compilation failure cannot count as a negative control')
    reports = []
    for name, marker, assertion in SCENARIOS:
        result = subprocess.run(
            [str(build / 'rodakos_voice_audio_frontend_identity_tests'), name],
            capture_output=True, text=True, timeout=15)
        combined = result.stdout + result.stderr
        report = {'scenario': name, 'returncode': result.returncode,
                  'stdout': result.stdout, 'stderr': result.stderr,
                  'detected': result.returncode == 1 and marker in combined
                  and assertion in combined and '1 tests, 1 failures' in combined}
        reports.append(report)
        if not report['detected']:
            raise AssertionError(f'Expected precise registry ownership failure: {report}')
    (args.output / 'results.json').write_text(json.dumps(
        {'baseline': BASELINE, 'frozenSha256': hashes, 'cases': reports}, indent=2) + '\n')


if __name__ == '__main__':
    main()
