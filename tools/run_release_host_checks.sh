#!/usr/bin/env bash
set -euo pipefail

rodak_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
rodak_checks="${1:-${XDG_CACHE_HOME:-$HOME/.cache}/rodakos-release}"
mkdir -p "$rodak_checks"
for rodak_suite in app_model home_ui ota_security; do
    rodak_target="$rodak_checks/asan-$rodak_suite"
    cmake -S "$rodak_root/tests/$rodak_suite" -B "$rodak_target" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
        '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
        >"$rodak_checks/$rodak_suite.log" 2>&1
    cmake --build "$rodak_target" -j 6 >>"$rodak_checks/$rodak_suite.log" 2>&1
    ASAN_OPTIONS=detect_leaks=1 ctest --test-dir "$rodak_target" --output-on-failure
done
python3 -m unittest discover -s "$rodak_root/tests/ota_security" -p 'test_*.py'
