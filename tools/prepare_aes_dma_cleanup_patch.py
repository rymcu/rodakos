"""Generate the pinned IDF AES allocation-cleanup overlay outside the SDK."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

PATCH_DIR = Path(__file__).resolve().parents[1] / 'patches/aes_dma_cleanup/6.0.2'
SOURCE = 'components/mbedtls/port/aes/dma/esp_aes_dma_core.c'
BEFORE = '''        output_buf = heap_caps_aligned_alloc(output_alignment, chunk_len, output_heap_caps);
        if (output_buf == NULL) {
            mbedtls_platform_zeroize(output, len);
            ESP_LOGE(TAG, "Failed to allocate memory");
            return -1;
        }'''
AFTER = BEFORE.replace('            return -1;', '            ret = -1;\n            goto cleanup;')


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def inputs(idf_path: Path) -> list[Path]:
    provenance = json.loads((PATCH_DIR / 'provenance.json').read_text())
    return [Path(__file__).resolve(), PATCH_DIR / 'provenance.json'] + [
        idf_path / name for name in provenance['idf_source_hashes_lf']]


def prepare(idf_path: Path, output_dir: Path) -> Path:
    idf_path, output_dir = idf_path.resolve(), output_dir.resolve()
    require(output_dir != idf_path and idf_path not in output_dir.parents,
            'AES generated output must be outside the IDF installation')
    repo = Path(__file__).resolve().parents[1]
    managed = (repo / 'managed_components').resolve()
    require(output_dir != managed and managed not in output_dir.parents,
            'AES generated output must be outside managed components')
    provenance = json.loads((PATCH_DIR / 'provenance.json').read_text())
    reviewed = {}
    for name, digest in provenance['idf_source_hashes_lf'].items():
        data = (idf_path / name).read_bytes().replace(b'\r\n', b'\n')
        actual = hashlib.sha256(data).hexdigest()
        require(actual == digest, f'Unreviewed AES source/metadata: {name} ({actual})')
        reviewed[name] = data
    text = reviewed[SOURCE].decode()
    require(text.count(BEFORE) == 1, 'Expected exactly one reviewed AES output allocation failure block')
    result = text.replace(BEFORE, AFTER, 1).encode()
    require(hashlib.sha256(result).hexdigest() == provenance['generated_sha256_lf'],
            'Generated AES cleanup source differs from reviewed patch')
    output = output_dir / 'esp_aes_dma_core.c'
    output_dir.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != result:
        temporary = output.with_suffix('.c.tmp')
        temporary.write_bytes(result)
        temporary.replace(output)
    return output


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
            print(f'AES DMA cleanup overlay verified: {prepare(args.idf_path, args.output_dir)}')
    except (OSError, ValueError, UnicodeError) as error:
        print(f'AES DMA cleanup overlay refused: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
