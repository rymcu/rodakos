"""Exact controls for feed-progress attribution; build failure is never a detection."""
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
source = root / 'main/phone_os/voice_feed_progress_observer.cc'
original = source.read_text(encoding='utf-8')
inputs = [source, root / 'main/phone_os/voice_feed_progress_observer.h'] + sorted(
    path for path in Path(__file__).parent.rglob('*') if path.is_file() and '__pycache__' not in str(path))
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
before = {str(path): sha(path) for path in inputs}
specs = [
    ('post-return-washed', 'overrun', 'if (last > api_return_us) out.flags |= kVoiceFeedProgressCloseOverrun;',
     'if (false) out.flags |= kVoiceFeedProgressCloseOverrun;',
     'post-return sample invalidates strict counts without subtracting aggregates'),
    ('clock-regression-washed', 'clock-regression',
     'if (valid && previous_end != 0 && getter_begin_us < previous_end)',
     'if (valid && previous_end != 0 && false)',
     'regressed sample cannot wash away a post-return endpoint'),
    ('missing-sequence-fence', 'open-fence',
     'live.epoch == epoch && live.sequence == sequence)', 'live.epoch == epoch)',
     'different feed sequence cannot borrow an active slot'),
    ('quota-renewed-by-epoch', 'quota', 'if (generation != live.generation) {',
     'if (generation != live.generation || epoch != live.epoch) {',
     'open and complete share eight-record quota across epochs'),
    ('failed-close-clears', 'busy-conflict',
     '    const int64_t last = out.last_target_end_us > out.last_other_end_us',
     '    if (out.status == kVoiceFeedProgressBusy) live.ticket = 0;\n    const int64_t last = out.last_target_end_us > out.last_other_end_us',
     'failed Close preserves the unfinished identity'),
    ('live-log-counts', 'threshold-frozen', 'out.last_other_handle, out.target_samples, out.other_samples,',
     'out.last_other_handle, live.target_samples, live.other_samples,',
     'logging reads frozen fields instead of newer live profile'),
    ('retire-keeps-authority', 'retirement',
     'if (observer.sampling_generation.load(std::memory_order_acquire) == generation)\n        observer.sampling_ticket.store(0, std::memory_order_release);',
     'if (observer.sampling_generation.load(std::memory_order_acquire) == generation)\n        observer.sampling_ticket.store(observer.sampling_ticket.load(std::memory_order_relaxed), std::memory_order_release);',
     'same opaque address after retirement is not attributed to the old owner'),
    ('partial-identity-revokes', 'retirement',
     'ticket.generation == live.generation && ticket.epoch == live.epoch &&\n        ticket.sequence == live.sequence && ticket.target_handle == live.target_handle &&',
     'ticket.generation == live.generation &&',
     'altered identity cannot revoke an otherwise matching ticket')
]
args.output.mkdir(parents=True, exist_ok=False)
rows = []
for name, case, old, new, assertion in specs:
    assert original.count(old) == 1, (name, original.count(old))
    folder = args.output / name
    folder.mkdir()
    variant = folder / source.name
    variant.write_text(original.replace(old, new, 1), encoding='utf-8', newline='\n')
    build = folder / 'build'
    commands = [
        ['cmake', '-S', str(Path(__file__).parent), '-B', str(build), '-G', 'Ninja',
         '-DCMAKE_BUILD_TYPE=Debug', '-DRODAK_FEED_PROGRESS_SOURCE=' + str(variant)],
        ['cmake', '--build', str(build), '--target', 'voice_feed_progress_tests'],
        [str(build / 'voice_feed_progress_tests'), case]
    ]
    results = []
    for index, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        log = folder / f'{index}.log'
        log.write_text(result.stdout + result.stderr, encoding='utf-8', newline='\n')
        results.append({'argv': command, 'exitCode': result.returncode, 'log': str(log), 'sha256': sha(log)})
        if index < 2: assert result.returncode == 0, (name, 'not a negative detection', result.stderr)
        else: assert result.returncode == 1 and 'ASSERTION: ' + assertion in result.stderr, (name, result.stdout, result.stderr)
    rows.append({'name': name, 'case': case, 'expectedAssertion': assertion,
                 'variantSha256': sha(variant), 'result': 'REJECTED_BY_EXACT_ASSERTION', 'commands': results})
after = {str(path): sha(path) for path in inputs}
assert before == after, 'Module/test inputs changed'
report = {'createdAtUtc': datetime.now(timezone.utc).isoformat(), 'status': 'PASS_EXACT_FEED_PROGRESS_CONTROLS',
          'sourceSha256': sha(source), 'inputsBefore': before, 'inputsAfter': after, 'controls': rows,
          'mainBuilds': 0, 'hardwareOperations': 0}
(args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'status': report['status'], 'controls': len(rows), 'report': str(args.output / 'report.json')}))
