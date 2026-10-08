"""Require the old driver function to fail the RCC ownership assertions."""
import argparse
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--baseline', required=True)
parser.add_argument('--patched', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
report = []
for scenario, message in [
    ('single', 'single deinit releases RCC reference'),
    ('repeat', 'repeated cycle has no RCC leak'),
    ('shared_owner', 'DVP release preserves other owner'),
    ('invalid', None),
]:
    for variant, executable in [('upstream', args.baseline), ('patched', args.patched)]:
        result = subprocess.run([executable, scenario], capture_output=True, text=True, timeout=10)
        output = result.stdout + result.stderr
        (args.output / f'{variant}-{scenario}.log').write_text(output)
        expected = 1 if variant == 'upstream' and message else 0
        if re.search(r'AddressSanitizer|LeakSanitizer|runtime error:', output):
            raise RuntimeError(f'Sanitizer failure in {variant} {scenario}:\n{output}')
        if result.returncode != expected:
            raise RuntimeError(f'Unexpected {variant} {scenario}: {result.returncode}\n{output}')
        if expected and f'FAIL {scenario}: {message}' not in output:
            raise RuntimeError(f'Negative control missed specified assertion for {scenario}:\n{output}')
        if not expected and f'PASS {scenario}' not in output:
            raise RuntimeError(f'Positive case did not pass for {variant} {scenario}:\n{output}')
        report.append({'variant': variant, 'scenario': scenario,
                       'returncode': result.returncode, 'expected': expected})
(args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print('Old RCC acquire logic rejected; patched single/repeat/shared-owner behavior passed')
