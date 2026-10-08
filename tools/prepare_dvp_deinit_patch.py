"""Generate the reviewed ESP Video DVP cleanup overlay without editing managed files."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

PATCH = Path(__file__).resolve().parents[1] / 'patches/dvp_deinit/2.3.0'
SOURCE = 'espressif__esp_video/src/esp_video_init.c'
GUARD = '''
#if ESP_VIDEO_ENABLE_SCCB_DEVICE && CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE
    if ((flags & ESP_VIDEO_INIT_FLAGS_DVP) && config->dvp != NULL && s_rodak_dvp_cleanup.pending) {
        _lock_release_recursive(&s_init_lock);
        ESP_LOGE(TAG, "DVP cleanup is incomplete; finish deinit before initializing");
        return ESP_ERR_INVALID_STATE;
    }
#endif
'''


def read_lf(path: Path) -> bytes:
    return path.read_bytes().replace(b'\r\n', b'\n')


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def function_text(source: str, name: str) -> str:
    definitions = list(re.finditer(r'(?m)^(?:static )?esp_err_t ' + re.escape(name) + r'\(', source))
    require(len(definitions) == 1, 'Expected exactly one function: ' + name)
    start = definitions[0].start()
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError('Unclosed function: ' + name)


def inputs(managed: Path, idf: Path) -> list[Path]:
    provenance = json.loads((PATCH / 'provenance.json').read_text())
    return [Path(__file__).resolve(), PATCH / 'provenance.json',
            PATCH / 'destroy_dvp_video_device.inc'] + [
                managed / name for name in provenance['managed_hashes_lf']] + [
                idf / name for name in provenance['idf_hashes_lf']]


def verify(managed: Path, idf: Path) -> dict[str, bytes]:
    provenance = json.loads((PATCH / 'provenance.json').read_text())
    reviewed = {}
    for name, expected in provenance['managed_hashes_lf'].items():
        content = read_lf(managed / name)
        actual = hashlib.sha256(content).hexdigest()
        require(actual == expected, f'Unreviewed DVP dependency: {name} ({actual})')
        reviewed[name] = content
    for name, expected in provenance['idf_hashes_lf'].items():
        actual = hashlib.sha256(read_lf(idf / name)).hexdigest()
        require(actual == expected, f'Unreviewed DVP IDF dependency: {name} ({actual})')
    return reviewed


def patch(source: str) -> str:
    old = function_text(source, 'destroy_dvp_video_device')
    replacement = read_lf(PATCH / 'destroy_dvp_video_device.inc').decode().rstrip()
    require('s_rodak_dvp_cleanup' not in source, 'DVP overlay already present')
    source = source.replace(old, replacement, 1)
    old_init = function_text(source, 'esp_video_init_with_flags')
    boundary = '    _lock_acquire_recursive(&s_init_lock);\n'
    require(old_init.count(boundary) == 1, 'DVP init lock boundary changed')
    source = source.replace(old_init, old_init.replace(boundary, boundary + GUARD, 1), 1)
    return source


def prepare(managed: Path, output: Path, idf: Path) -> Path:
    managed, output, idf = managed.resolve(), output.resolve(), idf.resolve()
    require(output != managed and managed not in output.parents,
            'DVP generated output must be outside managed components')
    require(output != idf and idf not in output.parents,
            'DVP generated output must be outside ESP-IDF')
    reviewed = verify(managed, idf)
    generated = patch(reviewed[SOURCE].decode()).encode()
    provenance = json.loads((PATCH / 'provenance.json').read_text())
    require(hashlib.sha256(generated).hexdigest() == provenance['generated_sha256_lf'],
            'Generated DVP overlay differs from reviewed patch')
    destination = output / 'esp_video_init.c'
    output.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_bytes() != generated:
        temporary = destination.with_suffix('.c.tmp')
        temporary.write_bytes(generated)
        temporary.replace(destination)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--managed-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--idf-path', type=Path, required=True)
    parser.add_argument('--print-inputs', action='store_true')
    args = parser.parse_args()
    try:
        if args.print_inputs:
            print('\n'.join(str(path) for path in inputs(args.managed_dir, args.idf_path)))
        else:
            print('DVP cleanup overlay verified: ' + str(prepare(args.managed_dir, args.output_dir, args.idf_path)))
    except (OSError, ValueError, UnicodeError) as error:
        print('DVP cleanup overlay refused: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
