#!/usr/bin/env bash
set -euo pipefail

OUT="${1:-/tmp/rodakos-site-ppms}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CACHE="$HOME/.cache/rodakos-site-screens"

mkdir -p "$OUT" "$CACHE"

echo "=== Running Home & Shell screenshot renderer ==="
if [ ! -f "$CACHE/home/build.ninja" ]; then
  cmake -S "$ROOT/tools/site_screenshots" -B "$CACHE/home" -G Ninja -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$CACHE/home"
"$CACHE/home/rodakos_site_screenshots" "$OUT"

build_and_run() {
  local suite="$1"
  local bdir="$CACHE/$suite"
  if [ ! -f "$bdir/build.ninja" ]; then
    cmake -S "$ROOT/tests/$suite" -B "$bdir" -G Ninja -DCMAKE_BUILD_TYPE=Release
  fi
  cmake --build "$bdir"
  local bin
  bin="$(find "$bdir" -maxdepth 1 -type f -executable -name '*ui*' | head -n 1)"
  (cd "$OUT" && TZ=UTC "$bin" >/dev/null)
  echo "=== Done $suite ==="
}

# Build remaining suites in parallel (up to 3 at once to keep WSL I/O fast)
pids=()
for suite in calendar_ui gyro_ui smart_ui assistant_ui recorder_ui themes_ui wol_ui; do
  build_and_run "$suite" &
  pids+=($!)
  if [ "${#pids[@]}" -ge 3 ]; then
    wait "${pids[0]}"
    pids=("${pids[@]:1}")
  fi
done
for pid in "${pids[@]}"; do
  wait "$pid"
done

echo "=== Generated PPM files ==="
ls -1 "$OUT"/*.ppm | wc -l
