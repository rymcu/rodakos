#!/usr/bin/env bash
set -euo pipefail

rodak_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
rodak_checks="${1:-${XDG_CACHE_HOME:-$HOME/.cache}/rodakos-release}"
rodak_idf_source="${RODAKOS_IDF_PATH:-${IDF_PATH:-}}"
if [[ ! -f "$rodak_idf_source/components/esp_event/esp_event.c" ]]; then
    echo 'Set RODAKOS_IDF_PATH (or IDF_PATH) to the reviewed ESP-IDF 6.0.2 source tree.' >&2
    exit 1
fi
export RODAKOS_IDF_PATH="$rodak_idf_source"
mkdir -p "$rodak_checks"
for rodak_suite in app_model home_ui assistant_ui ota_security codec_volume mqtt_event_patch mqtt_volume_service websocket_redirect_patch \
    serial_provisioning server_trust server_trust_nvs_storage wifi_adapter \
    voice_wake_service voice_volume_service voice_audio_frontend_identity voice_identity_integration \
    file_path_lease web_file_upload file_directory audio_playback_service music_ui file_writer \
    recording_service recorder_ui camera_capture camera_ui file_manager_ui photos_ui display_service lodepng_decode; do
    rodak_target="$rodak_checks/asan-$rodak_suite"
    rodak_suite_options=()
    if [[ "$rodak_suite" == mqtt_event_patch || "$rodak_suite" == websocket_redirect_patch || "$rodak_suite" == server_trust_nvs_storage ]]; then
        rodak_suite_options+=("-DRODAKOS_IDF_PATH=$rodak_idf_source")
    fi
    cmake -S "$rodak_root/tests/$rodak_suite" -B "$rodak_target" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
        '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
        "${rodak_suite_options[@]}" \
        >"$rodak_checks/$rodak_suite.log" 2>&1
    cmake --build "$rodak_target" -j 6 >>"$rodak_checks/$rodak_suite.log" 2>&1
    ASAN_OPTIONS=detect_leaks=1 ctest --test-dir "$rodak_target" --output-on-failure
done
python3 -m unittest discover -s "$rodak_root/tests/ota_security" -p 'test_*.py'
python3 -m unittest discover -s "$rodak_root/tests/codec_volume" -p 'test_*.py'
python3 -m unittest discover -s "$rodak_root/tests/mqtt_event_patch" -p 'test_*.py'
python3 -m unittest discover -s "$rodak_root/tests/websocket_redirect_patch" -p 'test_*.py'
