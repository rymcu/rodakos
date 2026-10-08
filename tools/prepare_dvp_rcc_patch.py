"""Generate the reviewed ESP-IDF DVP RCC reference-count overlay."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


PATCH_DIR = Path(__file__).resolve().parents[1] / 'patches/dvp_rcc/2.3.0'
SOURCE = 'components/esp_driver_cam/dvp/src/esp_cam_ctlr_dvp_cam.c'
FUNCTION = 'esp_cam_ctlr_dvp_deinit'


def read_lf(path: Path) -> bytes:
    return path.read_bytes().replace(b'\r\n', b'\n')


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def function_text(source: str, name: str) -> str:
    matches = list(re.finditer(r'(?m)^esp_err_t ' + re.escape(name) + r'\(', source))
    require(len(matches) == 1, f'Expected exactly one reviewed function: {name}')
    start = matches[0].start()
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError(f'Unclosed reviewed function: {name}')


def inputs(idf_path: Path) -> list[Path]:
    provenance = json.loads((PATCH_DIR / 'provenance.json').read_text())
    return [Path(__file__).resolve(), PATCH_DIR / 'provenance.json'] + [
        idf_path / name for name in provenance['idf_source_hashes_lf']
    ]


def verify(idf_path: Path) -> dict[str, bytes]:
    provenance = json.loads((PATCH_DIR / 'provenance.json').read_text())
    reviewed: dict[str, bytes] = {}
    for name, expected in provenance['idf_source_hashes_lf'].items():
        content = read_lf(idf_path / name)
        actual = hashlib.sha256(content).hexdigest()
        require(actual == expected, f'Unreviewed DVP RCC input: {name} ({actual})')
        reviewed[name] = content
    return reviewed


def patch(source: str) -> str:
    original = function_text(source, FUNCTION)
    old = 'PERIPH_RCC_ACQUIRE_ATOMIC(cam_periph_signals.buses[ctlr_id].module, ref_count)'
    require(original.count(old) == 1, 'Expected exactly one DVP deinit RCC acquire')
    replacement = original.replace(old, 'PERIPH_RCC_RELEASE_ATOMIC(cam_periph_signals.buses[ctlr_id].module, ref_count)', 1)
    return source.replace(original, replacement, 1)


def prepare(idf_path: Path, output_dir: Path) -> Path:
    idf_path, output_dir = idf_path.resolve(), output_dir.resolve()
    require(output_dir != idf_path and idf_path not in output_dir.parents,
            'DVP RCC generated output must be outside ESP-IDF')
    repo = Path(__file__).resolve().parents[1]
    managed = (repo / 'managed_components').resolve()
    require(output_dir != managed and managed not in output_dir.parents,
            'DVP RCC generated output must be outside managed components')
    reviewed = verify(idf_path)
    reviewed_source = reviewed[SOURCE].decode()
    provenance = json.loads((PATCH_DIR / 'provenance.json').read_text())
    reviewed_function = function_text(reviewed_source, FUNCTION)
    require(hashlib.sha256(reviewed_function.encode()).hexdigest() ==
            provenance['reviewed_deinit_function_sha256_lf'],
            'Reviewed DVP deinit function boundary differs from provenance')
    generated = patch(reviewed_source).encode()
    generated_source = generated.decode()
    require(hashlib.sha256(function_text(generated_source, FUNCTION).encode()).hexdigest() ==
            provenance['patched_deinit_function_sha256_lf'],
            'Patched DVP deinit function boundary differs from provenance')
    require(hashlib.sha256(generated).hexdigest() == provenance['generated_sha256_lf'],
            'Generated DVP RCC source differs from reviewed patch')
    destination = output_dir / 'esp_cam_ctlr_dvp_cam.c'
    output_dir.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_bytes() != generated:
        temporary = destination.with_suffix('.c.tmp')
        temporary.write_bytes(generated)
        temporary.replace(destination)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--idf-path', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--print-inputs', action='store_true')
    args = parser.parse_args()
    try:
        if args.print_inputs:
            print('\n'.join(str(path) for path in inputs(args.idf_path)))
        else:
            print(f'DVP RCC overlay verified: {prepare(args.idf_path, args.output_dir)}')
    except (OSError, ValueError, UnicodeError) as error:
        print(f'DVP RCC overlay refused: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
