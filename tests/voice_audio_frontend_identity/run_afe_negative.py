"""Require precise behavioral failures from complete old/mutated frontend translation units."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


BASELINE = 'e6edadcd8d72ea8300c8afe3b0ccc1bb359eb80c'
STARTUP_BASELINE = '78917fae1b9acb02010cef026facefc96a008c5c'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    suite = Path(__file__).resolve().parent
    root = suite.parents[1]
    source = (root / 'main/phone_os/voice_audio_frontend.cc').read_text()
    header = (root / 'main/phone_os/voice_audio_frontend.h').read_text()
    old_source = subprocess.run(
        ['git', '-C', str(root), 'show', BASELINE + ':main/phone_os/voice_audio_frontend.cc'],
        capture_output=True, check=True).stdout.decode()
    old_header = subprocess.run(
        ['git', '-C', str(root), 'show', BASELINE + ':main/phone_os/voice_audio_frontend.h'],
        capture_output=True, check=True).stdout.decode()
    startup_source = subprocess.run(
        ['git', '-C', str(root), 'show', STARTUP_BASELINE + ':main/phone_os/voice_audio_frontend.cc'],
        capture_output=True, check=True).stdout.decode()
    startup_header = subprocess.run(
        ['git', '-C', str(root), 'show', STARTUP_BASELINE + ':main/phone_os/voice_audio_frontend.h'],
        capture_output=True, check=True).stdout.decode()
    before = '''                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    afe_feed_started_ = true;
                    xSemaphoreGive(mutex_);
                    afe_iface_->feed(afe_data_, afe_feed_buffer.data());'''
    after = '''                    afe_iface_->feed(afe_data_, afe_feed_buffer.data());
                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    afe_feed_started_ = true;
                    xSemaphoreGive(mutex_);'''
    assert startup_source.count(before) == 1
    assert source.count('if (stopped) break;') == 1
    assert source.count('conversation_assembler_.InvalidateContinuity();') == 1
    cases = [
        ('old_cancelled_error', old_source, old_header,
         'AFE cancelled failure is accounted separately from active fetch errors',
         'AFE_CANCELLED_OBSERVED rejected_warnings=1 generation_changed=1',
         'check failed: control::RejectedWarningCount() == size_t{0}'),
        ('cancelled_early_exit', source.replace('if (stopped) break;', 'break;'), header,
         'AFE cancellation keeps fetching until the blocked feed lease can drain',
         'AFE_DRAIN_OBSERVED next_fetch=0 stop_returned=1',
         'check failed: fetched_to_drain'),
        ('lost_active_gap', source.replace('conversation_assembler_.InvalidateContinuity();',
                                          '/* negative: current failure loses continuity marker */'), header,
         'AFE active fetch error preserves PCM while marking only the gap frame invalid',
         'AFE_GAP_OBSERVED samples=1024 vad_valid=1 current_errors=1',
         'check failed: !(frame.vad_valid)'),
        ('old_startup_partial', startup_source, startup_header,
         'AFE complete output credit preserves partial startup PCM across an input stall',
         'AFE_STARTUP_OBSERVED lost_samples=160',
         'check failed: state.partial_lost == size_t{0}'),
        ('post_feed_flag_partial', startup_source.replace(before, after), startup_header,
         'AFE complete output credit preserves partial startup PCM across an input stall',
         'AFE_STARTUP_OBSERVED lost_samples=160',
         'check failed: state.partial_lost == size_t{0}'),
        ('stale_raw_read', source.replace('read_epoch == afe_stream_epoch_', 'true /* negative: stale raw read */'), header,
         'AFE resync epoch fences a pending raw read and the old local input tail',
         'AFE_EPOCH_OBSERVED old_feed_samples=2816 stale_after_reset=320',
         'check failed: stale == 0'),
        ('stale_local_tail', source.replace('feed_generation != generation || feed_epoch != stream_epoch',
                                           'feed_generation != generation /* negative: stale local tail */'), header,
         'AFE resync epoch fences a pending raw read and the old local input tail',
         'AFE_EPOCH_OBSERVED old_feed_samples=2816 stale_after_reset=64',
         'check failed: stale == 0'),
        ('lost_raw_gap', source.replace(
            '// 此 raw read 已消耗但未 AppendRaw；取消/旧代不计入当前 capture。\n                aec_diagnostic_capture_.MarkDiscontinuity(true, generation);',
            '/* negative: omitted same-generation raw discontinuity */'), header,
         'AFE resync epoch fences a pending raw read and the old local input tail',
         'AFE_RAW_GAP_OBSERVED raw_samples=960 raw_discontinuities=0',
         'check failed: diagnostic.raw_discontinuities == 1U')
    ]
    reports = []
    for name, content, header_content, scenario, marker, assertion in cases:
        directory = args.output / name
        directory.mkdir(parents=True, exist_ok=True)
        fixture_source = directory / 'voice_audio_frontend.cc'
        fixture_source.write_text(content)
        includes = directory / 'include'
        fixture_header = includes / 'phone_os/voice_audio_frontend.h'
        fixture_header.parent.mkdir(parents=True, exist_ok=True)
        fixture_header.write_text(header_content)
        build = directory / 'build'
        commands = [
            ['cmake', '-S', str(suite), '-B', str(build), '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Debug', '-DRODAK_FRONTEND_NEGATIVE_CHILD=ON',
             f'-DRODAKOS_IDF_PATH={args.idf_path}',
             f'-DRODAK_VOICE_FRONTEND_SOURCE={fixture_source}',
             f'-DRODAK_VOICE_FRONTEND_INCLUDE_ROOT={includes}'],
            ['cmake', '--build', str(build), '-j', '4']
        ]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            (directory / f'build-{index}.log').write_text(result.stdout + result.stderr)
            if result.returncode:
                raise AssertionError(f'{name}: compilation failure is not a detected negative')
        result = subprocess.run([str(build / 'rodakos_voice_audio_frontend_identity_tests'), scenario],
                                capture_output=True, text=True, timeout=15)
        combined = result.stdout + result.stderr
        detected = result.returncode == 1 and marker in combined and assertion in combined
        detected = detected and '1 tests, 1 failures' in combined
        baseline = STARTUP_BASELINE if name in ['old_startup_partial', 'post_feed_flag_partial'] else BASELINE if name.startswith('old') else None
        report = {'name': name, 'scenario': scenario, 'baseline': baseline,
                  'sourceSha256': hashlib.sha256(content.encode()).hexdigest(),
                  'headerSha256': hashlib.sha256(header_content.encode()).hexdigest(),
                  'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
                  'detected': detected}
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        reports.append(report)
        if not detected:
            raise AssertionError(f'{name}: expected precise lifecycle assertion: {report}')
    (args.output / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')


if __name__ == '__main__':
    main()
