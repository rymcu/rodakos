"""Compile complete mutated production TUs and require two exact diagnostic assertion failures."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--idf-path', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    suite = Path(__file__).resolve().parent
    root = suite.parents[1]
    source = (root / 'main/phone_os/voice_audio_frontend.cc').read_text()
    header = (root / 'main/phone_os/voice_audio_frontend.h').read_text()
    diagnostic = (root / 'main/phone_os/voice_afe_observation.h').read_text()
    return_marker = '''                    const int64_t api_return_us = esp_timer_get_time();
                    afe_producer_diagnostics_.Publish(AfeProducerStage::kReturnedWaitPublish, generation,
                        stream_epoch, feed_call, api_return_us, written, AfeElapsedUs(api_begin_us, api_return_us));
                    xSemaphoreTake(mutex_, portMAX_DELAY);'''
    late_return = '''                    xSemaphoreTake(mutex_, portMAX_DELAY);
                    const int64_t api_return_us = esp_timer_get_time();
                    afe_producer_diagnostics_.Publish(AfeProducerStage::kReturnedWaitPublish, generation,
                        stream_epoch, feed_call, api_return_us, written, AfeElapsedUs(api_begin_us, api_return_us));'''
    release_before_log = '''                xSemaphoreGive(mutex_);
#if defined(RODAKOS_RELEASE_TESTS)
                tick_token = ObserveVoiceTickBoundary("stall", generation, epoch, tick_token,
                    gap.first_stall, observed_us);
#endif
                ESP_LOGW(TAG, "AFE input stalled:'''
    after_log = '                LogAfeProducerObservation(producer, generation, epoch, stalls, gap.first_stall, observed_us);'
    assert source.count(return_marker) == source.count(release_before_log) == source.count(after_log) == 1
    cases = [
        ('return_after_business_lock', source.replace(return_marker, late_return),
         'AFE SDK return is visible before the frontend credit publication lock',
         'AFE_RETURN_OBSERVED returned_visible=0', 'check failed: returned_visible'),
        ('stall_log_inside_business_lock', source.replace(release_before_log,
             release_before_log.replace('                xSemaphoreGive(mutex_);\n', '', 1)).replace(after_log,
             after_log + '\n                xSemaphoreGive(mutex_);'),
         'AFE stalled logger never owns the credit publication mutex',
         'AFE_LOG_UNLOCK_OBSERVED producer_progressed=0', 'check failed: producer_progressed')]
    reports = []
    for name, content, scenario, marker, assertion in cases:
        directory = args.output / name
        directory.mkdir(parents=True, exist_ok=True)
        fixture_source = directory / 'voice_audio_frontend.cc'
        fixture_source.write_text(content)
        includes = directory / 'include'
        (includes / 'phone_os').mkdir(parents=True, exist_ok=True)
        (includes / 'phone_os/voice_audio_frontend.h').write_text(header)
        (includes / 'phone_os/voice_afe_observation.h').write_text(diagnostic)
        build = directory / 'build'
        commands = [
            ['cmake', '-S', str(suite), '-B', str(build), '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Debug', '-DRODAK_FRONTEND_NEGATIVE_CHILD=ON',
             '-DRODAK_FRONTEND_OBSERVATION_TESTS=ON',
             f'-DRODAKOS_IDF_PATH={args.idf_path}',
             f'-DRODAK_VOICE_FRONTEND_SOURCE={fixture_source}',
             f'-DRODAK_VOICE_FRONTEND_INCLUDE_ROOT={includes}'],
            ['cmake', '--build', str(build), '-j', '4']]
        for index, command in enumerate(commands):
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            (directory / f'build-{index}.log').write_text(result.stdout + result.stderr)
            if result.returncode:
                raise AssertionError(f'{name}: build failure is not a detected negative')
        result = subprocess.run([str(build / 'rodakos_voice_audio_frontend_identity_tests'), scenario],
                                capture_output=True, text=True, timeout=15)
        combined = result.stdout + result.stderr
        detected = result.returncode == 1 and marker in combined and assertion in combined
        detected = detected and '1 tests, 1 failures' in combined
        report = {'name': name, 'scenario': scenario,
                  'sourceSha256': hashlib.sha256(content.encode()).hexdigest(),
                  'headerSha256': hashlib.sha256(header.encode()).hexdigest(),
                  'diagnosticHeaderSha256': hashlib.sha256(diagnostic.encode()).hexdigest(),
                  'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
                  'detected': detected}
        (directory / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        reports.append(report)
        if not detected:
            raise AssertionError(f'{name}: exact diagnostic assertion was not detected: {report}')
    (args.output / 'results.json').write_text(json.dumps(reports, indent=2) + '\n')


if __name__ == '__main__':
    main()
