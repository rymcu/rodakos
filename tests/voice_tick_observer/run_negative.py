"""Compile precise mutations of the actual observer; only a named assertion is a hit."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = root / 'main/phone_os/voice_tick_observer.cc'
original = source.read_text(encoding='utf-8')
args.output.mkdir(parents=True, exist_ok=True)
inputs = [source, root / 'main/phone_os/voice_tick_observer.h'] + sorted(
    path for path in Path(__file__).parent.rglob('*') if path.is_file() and '__pycache__' not in str(path))
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
before = {str(path): sha(path) for path in inputs}
private_line = '    core.tick.previous_call_us = clock_valid ? entry_us : 0;\n'
assert original.count(private_line) == 1
mutations = [
    ('published-predecessor', original.replace(private_line, '').replace(
        '    auto& out = core.published;\n', '    auto& out = core.published;\n' + private_line),
     'private ISR predecessor advances even on failed publish'),
    ('drop-cross-window', original.replace(
        'if (previous_us != 0 && entry_us >= out.window_open_us &&',
        'if (previous_us >= out.window_open_us && entry_us >= out.window_open_us &&'),
     'cross-window predecessor interval retained without boot maximum'),
    ('missing-token-fence', original.replace('control.token == expected_token)', 'true)'),
     'old token cannot clear new scope'),
    ('live-log', original.replace('const auto& value = snapshot.cores[core];',
                                 'const auto& value = g_voice_tick_observer.cores[core].published;'),
     'log uses frozen copy after live updates')
]
rows = []
for name, text, expected in mutations:
    assert text != original, name
    folder = args.output / name
    folder.mkdir(exist_ok=False)
    variant = folder / 'voice_tick_observer.cc'
    variant.write_text(text, encoding='utf-8', newline='\n')
    build = folder / 'build'
    commands = [
        ['cmake', '-S', str(Path(__file__).parent), '-B', str(build), '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Debug', '-DRODAK_OBSERVER_SOURCE=' + str(variant)],
        ['cmake', '--build', str(build)],
        [str(build / 'voice_tick_observer_tests')]
    ]
    for index, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True, timeout=45)
        output = result.stdout + result.stderr
        (folder / f'{index}.log').write_text(output, encoding='utf-8', newline='\n')
        if index < 2:
            assert result.returncode == 0, (name, 'build is not a negative hit', output)
        else:
            assert result.returncode != 0 and 'ASSERTION: ' + expected in output, (name, output)
    rows.append({'name': name, 'variantSha256': sha(variant), 'expectedAssertion': expected,
                 'result': 'REJECTED_BY_EXACT_ASSERTION'})
after = {str(path): sha(path) for path in inputs}
assert before == after, 'Sources changed while running negatives'
report = {'createdAtUtc': datetime.now(timezone.utc).isoformat(), 'status': 'PASS',
          'actualModuleSourceSha256': sha(source), 'inputsBefore': before, 'inputsAfter': after,
          'controls': rows, 'deviceOperations': 0, 'mainBuilds': 0}
(args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'status': 'PASS', 'negativeControls': len(rows), 'report': str(args.output / 'report.json')}))
