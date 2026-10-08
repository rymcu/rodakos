"""Require named failures from complete-TU capture/snapshot regressions."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--idf-path', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=False)
    assistant = root / 'main/phone_os/voice_assistant_service.cc'
    wake = root / 'main/phone_os/voice_wake_service.cc'
    inputs = {path: path.read_bytes() for path in (assistant, wake)}
    variants = [
        ('full-snapshot-getter', assistant,
         '''    xSemaphoreTake(mutex_, portMAX_DELAY);
    const VoiceAssistantPhase phase = phase_;
    xSemaphoreGive(mutex_);
    return phase;''',
         '    return GetState().phase;',
         'phase snapshot follows real assistant phases', 'narrow.count == 0u'),
        ('missing-terminal-failure', assistant,
         'if (recording_expected && !recorder_.IsRunning()) {',
         'if (false && recording_expected && !recorder_.IsRunning()) {',
         'terminal capture failure exits real listening', 'failed'),
        ('missing-transport-guard', assistant,
         '         transport_generation_ != expected_recording_transport_generation ||\n', '',
         'delayed capture failure endpoint rejects an old transport',
         'state.phase == rodakos::VoiceAssistantPhase::kListening'),
        ('wake-full-snapshot', wake,
         'const VoiceAssistantPhase assistant_phase = assistant_.GetPhaseSnapshot();',
         'const VoiceAssistantPhase assistant_phase = assistant_.GetState().phase;',
         'wake callback reads phase without copying the full assistant snapshot', 'full_reads == 0u'),
        ('wake-missing-generation-recheck', wake,
         'const bool interrupted = can_interrupt && assistant_.InterruptSpeaking();',
         'const bool interrupted = assistant_.InterruptSpeaking();',
         'wake callback invalidated while reading assistant phase cannot interrupt later TTS',
         'f.assistant.interrupts == 0u'),
    ]
    records = []
    for name, source, before, after, scenario, assertion in variants:
        text = inputs[source].decode().replace('\r\n', '\n')
        if text.count(before) != 1:
            raise RuntimeError(name + ': mutation boundary drifted')
        folder = args.output / name
        folder.mkdir()
        candidate = folder / source.name
        candidate.write_text(text.replace(before, after, 1), newline='\n')
        suite = 'voice_volume_service' if source == assistant else 'voice_wake_service'
        option = 'VOICE_ASSISTANT_SOURCE' if source == assistant else 'VOICE_WAKE_SOURCE'
        build = folder / 'build'
        commands = [
            ['cmake', '-S', str(root / 'tests' / suite), '-B', str(build), '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Debug', '-DRODAKOS_IDF_PATH=' + args.idf_path,
             '-DRODAK_WAKE_NEGATIVE_CHILD=ON', '-D' + option + '=' + str(candidate)],
            ['cmake', '--build', str(build), '-j', '2']]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, timeout=180)
            (folder / f'build-{index}.log').write_bytes(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(name + ': build failure is not detection')
        command = [str(build / ('rodakos_' + suite + '_tests'))]
        command += ['--filter', scenario] if source == assistant else [scenario]
        result = subprocess.run(command, capture_output=True, timeout=20)
        output = result.stdout + result.stderr
        (folder / 'result.log').write_bytes(output)
        decoded = output.decode(errors='replace')
        expected = ['1 tests, 1 failures', 'check failed: ' + assertion]
        if result.returncode != 1 or not all(value in decoded for value in expected):
            raise RuntimeError(name + ': intended assertion missing\n' + decoded)
        if any(value in decoded for value in ('AddressSanitizer', 'LeakSanitizer', 'runtime error:')):
            raise RuntimeError(name + ': sanitizer errors are not detection')
        records.append({'name': name, 'command': command, 'expected': expected, 'exitCode': result.returncode,
                        'candidateSha256': hashlib.sha256(candidate.read_bytes()).hexdigest(),
                        'resultSha256': hashlib.sha256(output).hexdigest(), 'detected': True})
        (args.output / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
        print('Detected ' + name, flush=True)
    for path, original in inputs.items():
        if path.read_bytes() != original:
            raise RuntimeError('Source changed during controls: ' + str(path))


if __name__ == '__main__':
    main()
