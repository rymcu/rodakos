"""Validate actual dispatcher JSON and the macro-off translation unit."""
import argparse
import json
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', required=True)
parser.add_argument('--production-object', required=True)
args = parser.parse_args()
wire = subprocess.run([args.executable, '--wire'], check=True, capture_output=True, text=True, timeout=5)
events = []
for line in wire.stdout.splitlines():
    prefix = 'RODAK_VOICE_CYCLE '
    if not line.startswith(prefix):
        raise AssertionError('Unexpected wire output: ' + line)
    events.append(json.loads(line[len(prefix):]))
assert [event['phase'] for event in events] == ['accepted', 'before', 'after', 'recovered', 'complete']
assert all(event['id'] == 4294967295 for event in events)
assert events[0]['result'] == 'queued'
assert events[1]['assistant_phase'] == 'listening'
assert all(events[1][task] for task in ['assistant', 'capture', 'supervisor'])
assert not any(events[2][task] for task in ['assistant', 'capture', 'supervisor'])
assert not events[3]['assistant'] and events[3]['capture'] and events[3]['supervisor']
assert events[4]['result'] == 'three_task_pass'
assert events[4]['stopped'] and events[4]['recovered'] and events[4]['listening_after']
symbols = subprocess.run(['nm', '-C', '--defined-only', args.production_object],
                         check=True, capture_output=True, text=True).stdout
assert not symbols.strip(), 'Macro-off production object must have no diagnostic symbols'
print('actual JSON phases/IDs/task checks passed; macro-off TU exports no symbols')
