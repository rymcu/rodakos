"""Require the reviewed upstream functions to fail through specific ownership assertions."""
import argparse
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', required=True)
parser.add_argument('--patched', required=True)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
report = []
for scenario, message in [
    ('normal', None),
    ('video_failure', 'VFS failure must preserve published sensor and SCCB'),
    ('retry_after_sensor_failure', 'retry must not reuse released handles'),
    ('retry_after_port_failure', 'retry must not reuse released handles'),
]:
    for variant, executable in [('upstream', args.baseline), ('patched', args.patched)]:
        result = subprocess.run([executable, scenario], capture_output=True, text=True, timeout=10)
        text = result.stdout + result.stderr
        (args.output / f'{variant}-{scenario}.log').write_text(text)
        expected = 1 if variant == 'upstream' and message else 0
        if (result.returncode != expected or
                re.search(r'AddressSanitizer|LeakSanitizer|runtime error:', text) or
                (expected and f'FAIL {scenario}: {message}' not in text) or
                (not expected and f'PASS {scenario}' not in text)):
            raise RuntimeError(f'Unexpected {variant} {scenario}: {result.returncode}\n{text}')
        if variant == 'upstream' and scenario == 'retry_after_sensor_failure':
            if 'Rejected access to retired SCCB handle' not in text:
                raise RuntimeError('Negative did not reach the retired SCCB boundary')
        report.append({'variant': variant, 'scenario': scenario,
                       'returncode': result.returncode, 'expected': expected})
(args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print('3 reviewed upstream regressions rejected; both normal paths pass')
