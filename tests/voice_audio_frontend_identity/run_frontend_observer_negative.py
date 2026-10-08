"""Require exact assertions from complete TEST frontend ordering regressions."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    suite = Path(__file__).resolve().parent
    root = suite.parents[1]
    source = (root / 'main/phone_os/voice_audio_frontend.cc').read_text()
    observer = (root / 'main/phone_os/voice_feed_progress_observer.cc').read_text()
    arm = '''                    const auto feed_progress_ticket = ArmVoiceFeedProgress(
                        generation, stream_epoch, feed_call,
                        reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()),
                        api_begin_us, afe_started_us);'''
    feed = '                    const int written = afe_iface_->feed(afe_data_, afe_feed_buffer.data());'
    close = '                    CloseVoiceFeedProgress(feed_progress_ticket, api_return_us, feed_progress);'
    returned = '''                    afe_producer_diagnostics_.Publish(AfeProducerStage::kReturnedWaitPublish, generation,
                        stream_epoch, feed_call, api_return_us, written, AfeElapsedUs(api_begin_us, api_return_us));
                    xSemaphoreTake(mutex_, portMAX_DELAY);'''
    producer_guard = '''                if (producer.stage == AfeProducerStage::kApiBoundary &&
                    producer.generation == generation && producer.epoch == epoch) {'''
    assert all(source.count(text) == 1 for text in [arm, feed, close, returned, producer_guard])
    cases = [
        ('arm_after_sdk_entry', source.replace(arm, '').replace(feed,
            feed + '\n#if defined(RODAKOS_RELEASE_TESTS)\n' + arm + '\n#endif'),
         'TEST frontend arms the actual feed before SDK entry and classifies the same capture handle',
         'TEST_FRONTEND_FEED_ENTRY armed_before_feed=0 feeds=1', 'check failed: armed_before_feed'),
        ('close_after_publication_lock', source.replace(close, '').replace(returned,
            returned + '\n#if defined(RODAKOS_RELEASE_TESTS)\n' + close + '\n#endif'),
         'TEST frontend closes feed observation before waiting for credit publication',
         'TEST_FRONTEND_CLOSE_BOUNDARY closed_before_publish=0', 'check failed: closed_before_publish'),
        ('read_sequence_borrows_feed_scope', source.replace(producer_guard, '                if (true) {'),
         'TEST frontend never lends an unfinished feed slot to the same numeric read sequence',
         'TEST_FRONTEND_READ_SCOPE paused=1 read_seq=1 feed_seq=1 read_scope_rejected=0',
         'check failed: read_scope_rejected')]
    cases = [(*case, None) for case in cases]
    close_begin = observer.index('void CloseVoiceFeedProgress(')
    close_end = observer.index('void SnapshotOpenVoiceFeedProgress(', close_begin)
    close_body = observer[close_begin:close_end]
    revoke = '        g_voice_feed_progress_observer.sampling_ticket.store(0, std::memory_order_release);'
    assert close_body.count(revoke) == 1
    no_close_revoke = observer[:close_begin] + close_body.replace(revoke, '') + observer[close_end:]
    cases.append(('failed_close_keeps_sampling', source,
        'TEST frontend retires failed close sampling before capture deletion and opaque reuse',
        'TEST_FRONTEND_RETIRE failed_close_stops_sampling=0 retired_handle_not_sampled=1',
        'check failed: failed_close_stops_sampling', no_close_revoke))
    reports = []
    for name, changed, scenario, marker, assertion, changed_observer in cases:
        directory = args.output / name
        directory.mkdir(parents=True, exist_ok=False)
        frozen = directory / 'voice_audio_frontend.cc'
        frozen.write_text(changed)
        includes = directory / 'include/phone_os'
        includes.mkdir(parents=True)
        for filename in ['voice_audio_frontend.h', 'voice_afe_observation.h']:
            (includes / filename).write_bytes((root / 'main/phone_os' / filename).read_bytes())
        build = directory / 'build'
        commands = [
            ['cmake', '-S', str(suite), '-B', str(build), '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Debug', '-DRODAK_FRONTEND_NEGATIVE_CHILD=ON',
             '-DRODAK_FRONTEND_TEST_OBSERVER_INTEGRATION=ON',
             '-DRODAKOS_IDF_PATH=' + args.idf_path,
             '-DRODAK_VOICE_FRONTEND_SOURCE=' + str(frozen),
             '-DRODAK_VOICE_FRONTEND_INCLUDE_ROOT=' + str(includes.parent)],
            ['cmake', '--build', str(build), '--target', 'rodakos_voice_frontend_observer_tests', '-j', '4']]
        if changed_observer is not None:
            observer_path = directory / 'voice_feed_progress_observer.cc'
            observer_path.write_text(changed_observer)
            commands[0].append('-DRODAK_FEED_PROGRESS_SOURCE=' + str(observer_path))
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            (directory / f'build-{index}.log').write_text(result.stdout + result.stderr)
            if result.returncode:
                raise AssertionError(f'{name}: compile failure is not a detected negative')
        result = subprocess.run([str(build / 'rodakos_voice_frontend_observer_tests'), scenario],
                                capture_output=True, text=True, timeout=20)
        combined = result.stdout + result.stderr
        detected = result.returncode == 1 and marker in combined and assertion in combined
        detected = detected and '1 tests, 1 failures' in combined
        report = {'name': name, 'scenario': scenario, 'sourceSha256': hashlib.sha256(changed.encode()).hexdigest(),
                  'observerSha256': hashlib.sha256((changed_observer or observer).encode()).hexdigest(),
                  'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
                  'detected': detected}
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        reports.append(report)
        if not detected:
            raise AssertionError(f'{name}: expected precise ordering assertion absent: {report}')
    (args.output / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')


if __name__ == '__main__':
    main()
