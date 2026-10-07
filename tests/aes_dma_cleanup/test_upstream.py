"""Require a specific old-source red assertion and identical successful traces."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--patched', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
# The old source intentionally loses its input allocation. Judge this control by
# the explicit ownership assertion; never turn a sanitizer crash into a red pass.
env['ASAN_OPTIONS'] = 'detect_leaks=0'
red = subprocess.run([str(args.baseline), 'output_failure'], capture_output=True, text=True,
                     env=env, timeout=10)
log = red.stdout + red.stderr
(args.output / 'baseline-red.log').write_text(log)
assert red.returncode == 1 and 'ASSERTION FAILED: state.outstanding_bytes == 0' in log, log
assert 'AddressSanitizer' not in log and 'runtime error:' not in log, log
traces = []
for name, path in [('baseline', args.baseline), ('patched', args.patched)]:
    result = subprocess.run([str(path), 'success'], capture_output=True, text=True, timeout=10)
    (args.output / f'{name}-success.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    assert not result.stderr, result.stderr
    traces.append(result.stdout)
assert traces[0] == traces[1], 'Successful byte checks and allocation traces changed'
manifest = {
    'upstreamFailureExit': red.returncode,
    'expectedAssertion': 'state.outstanding_bytes == 0',
    'redLeakDetectionDisabledOnly': True,
    'normalTraceExactlyEqual': True,
    'binaries': {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in (args.baseline, args.patched)},
}
(args.output / 'result.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Upstream ownership assertion detected; successful production traces identical.')
