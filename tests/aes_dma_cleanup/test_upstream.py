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
# 103 negative control: the upstream source takes the PSRAM output bounce from internal DMA RAM.
psram_red = subprocess.run([str(args.baseline), 'psram_output_bounce'], capture_output=True,
                           text=True, timeout=10)
psram_log = psram_red.stdout + psram_red.stderr
(args.output / 'baseline-psram-red.log').write_text(psram_log)
assert psram_red.returncode == 1 and 'ASSERTION FAILED: state.requested_caps[0] ==' in psram_log, psram_log
assert 'AddressSanitizer' not in psram_log and 'runtime error:' not in psram_log, psram_log
traces = []
for name, path in [('baseline', args.baseline), ('patched', args.patched)]:
    result = subprocess.run([str(path), 'success'], capture_output=True, text=True, timeout=10)
    (args.output / f'{name}-success.log').write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    assert not result.stderr, result.stderr
    traces.append(result.stdout)
# 103 changes only the PSRAM output bounce: its size is rounded up to the cache line. Lines
# without an output bounce (output_external=0) must stay identical; byte checks pass in both.
def unchanged(trace):
    return [line for line in trace.splitlines() if 'output_external=0' in line]
assert unchanged(traces[0]) == unchanged(traces[1]), 'Traces without a PSRAM output bounce changed'
assert len(traces[0].splitlines()) == len(traces[1].splitlines()), 'Successful scenario count changed'
manifest = {
    'upstreamFailureExit': red.returncode,
    'expectedAssertion': 'state.outstanding_bytes == 0',
    'psramBounceUpstreamFailureExit': psram_red.returncode,
    'psramBounceExpectedAssertion': 'state.requested_caps[0] == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)',
    'redLeakDetectionDisabledOnly': True,
    'normalTraceWithoutPsramBounceEqual': True,
    'binaries': {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in (args.baseline, args.patched)},
}
(args.output / 'result.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Upstream ownership and PSRAM-bounce assertions detected; traces without a PSRAM bounce identical.')
