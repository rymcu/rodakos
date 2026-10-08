"""Inspect the ordinary full transport object, not the final target firmware."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--object', required=True)
parser.add_argument('--nm', required=True)
parser.add_argument('--objcopy', required=True)
args = parser.parse_args()
path = Path(args.object)
symbols = subprocess.check_output([args.nm, '-C', str(path)], text=True)
with tempfile.TemporaryDirectory(dir=path.parent) as temporary:
    stripped = Path(temporary) / 'without-debug.o'
    # The test directory name contains the observer name in DWARF paths.
    subprocess.check_call([args.objcopy, '--strip-debug', str(path), str(stripped)])
    for marker in ('VoicePreparePriority', 'VOICE_PREPARE_PRIORITY', 'voice_prepare_priority'):
        assert marker not in symbols, 'OFF transport retains an observer symbol or reference: ' + marker
        assert marker.encode() not in stripped.read_bytes(), 'OFF transport retains an observer marker: ' + marker
assert 'RodakRealtimeVoiceTransport::PrepareInteraction' in symbols
print('PASS ordinary full transport object has no priority-observer symbols/references; SHA256=' + hashlib.sha256(path.read_bytes()).hexdigest())
