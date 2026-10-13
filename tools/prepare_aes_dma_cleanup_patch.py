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

# Second reviewed change (2026-10-13, 103): on ESP32-S3 an unaligned PSRAM output needs a
# bounce buffer, which IDF takes from internal DMA RAM (up to 1,600 B per AES call). WebRTC
# encrypts SRTP in place in PSRAM, so Camera starved internal DMA RAM and AES failed with
# "Failed to allocate memory". S3 GDMA reaches PSRAM, so a PSRAM output bounce is taken from
# PSRAM, aligned to the data cache line in address and length. The S3 DMA path already syncs a
# PSRAM output before and after the transfer, and a cache-line-aligned buffer needs no further
# bounce. A non-DMA input (DROM/iCache) still uses internal DMA RAM.
PSRAM_BOUNCE_BEFORE = '''#endif /* SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE */

    if (realloc_input) {
        input_buf = heap_caps_aligned_alloc(input_alignment, chunk_len, input_heap_caps);'''
PSRAM_BOUNCE_AFTER = '''#endif /* SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE */

#if !SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE && CONFIG_SPIRAM && SOC_PSRAM_DMA_CAPABLE
    /* Rodak: keep a PSRAM output bounce in PSRAM instead of internal DMA RAM. The sub-line tail
       of each chunk goes to this static buffer, which the held AES hardware lock protects. */
    static DRAM_ATTR uint8_t s_rodak_tail_out[64];
    bool psram_output_bounce = false;
    size_t output_alloc_len = chunk_len;
    if (realloc_output && esp_ptr_external_ram(output)) {
        size_t psram_line = 0;
        if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &psram_line) == ESP_OK && psram_line != 0 &&
            psram_line <= sizeof(s_rodak_tail_out) && psram_line % AES_BLOCK_BYTES == 0) {
            psram_output_bounce = true;
            output_alignment = psram_line;
            output_alloc_len = (chunk_len + psram_line - 1) / psram_line * psram_line;
            output_heap_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        }
    }
#else
    const size_t output_alloc_len = chunk_len;
#endif

    if (realloc_input) {
        input_buf = heap_caps_aligned_alloc(input_alignment, chunk_len, input_heap_caps);'''
PSRAM_OUTPUT_ALLOC_BEFORE = '''        output_buf = heap_caps_aligned_alloc(output_alignment, chunk_len, output_heap_caps);'''
PSRAM_OUTPUT_ALLOC_AFTER = '''        output_buf = heap_caps_aligned_alloc(output_alignment, output_alloc_len, output_heap_caps);'''
# A PSRAM output must cover whole cache lines in the S3 DMA path, or that path bounces again.
# Each chunk therefore runs as a cache-line-aligned head into the PSRAM bounce and a sub-line
# tail into s_rodak_tail_out. Heads are multiples of 16 B, so the AES chaining state is kept.
PSRAM_LOOP_BEFORE = '''        if (esp_aes_process_dma(ctx, dma_input, output_buf, chunk_len, stream_out) != 0) {'''
PSRAM_LOOP_AFTER = '''#if !SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE && CONFIG_SPIRAM && SOC_PSRAM_DMA_CAPABLE
        if (psram_output_bounce) {
            size_t head = chunk_len / output_alignment * output_alignment;
            size_t tail = chunk_len - head;
            if ((head != 0 && esp_aes_process_dma(ctx, dma_input, output_buf, head, stream_out) != 0) ||
                (tail != 0 && esp_aes_process_dma(ctx, dma_input + head, s_rodak_tail_out, tail, stream_out) != 0)) {
                ret = -1;
                goto cleanup;
            }
            if (tail != 0) {
                memcpy(output_buf + head, s_rodak_tail_out, tail);
            }
        } else
#endif
        if (esp_aes_process_dma(ctx, dma_input, output_buf, chunk_len, stream_out) != 0) {'''

# Release-test only: log where AES-DMA operands live when they need an internal bounce buffer.
# The reviewed cleanup output stays byte-identical; the diagnostic is appended after its hash check.
DIAG_ANCHOR = '''    /* If either input or output is unaccessible to the DMA then they need to be reallocated */
    if (input_needs_realloc || output_needs_realloc) {'''
DIAG_HELPER = '''
static const char *rodak_aes_region(const void *p)
{
    if (esp_ptr_external_ram(p)) return "psram";
    if (esp_ptr_in_drom(p)) return "drom";
    if (esp_ptr_dma_capable(p)) return "dma";
    if (esp_ptr_internal(p)) return "internal-nodma";
    return "other";
}

static void rodak_aes_bounce_diag(const void *in, const void *out, size_t len, bool in_re, bool out_re)
{
    static uint32_t count;
    static size_t max_len;
    count++;
    if (len > max_len) max_len = len;
    if (count <= 8 || (count & (count - 1)) == 0) {
        ESP_LOGW("RodakAesDiag", "bounce n=%u len=%u max=%u in=%s%s %p out=%s%s %p dma_largest=%u psram_largest=%u",
                 (unsigned)count, (unsigned)len, (unsigned)max_len,
                 rodak_aes_region(in), in_re ? "*" : "", in,
                 rodak_aes_region(out), out_re ? "*" : "", out,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    }
}
'''
DIAG_CALL = '''    /* If either input or output is unaccessible to the DMA then they need to be reallocated */
    if (input_needs_realloc || output_needs_realloc) {
        rodak_aes_bounce_diag(input, output, len, input_needs_realloc, output_needs_realloc);'''
# ESP32-S3 compiles the #else branch of SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE, indented one level deeper.
DIAG_ANCHOR_S3 = '''        /* If either input or output is unaccessible to the DMA then they need to be reallocated */
        if (input_needs_realloc || output_needs_realloc) {'''
DIAG_CALL_S3 = '''        /* If either input or output is unaccessible to the DMA then they need to be reallocated */
        if (input_needs_realloc || output_needs_realloc) {
            rodak_aes_bounce_diag(input, output, len, input_needs_realloc, output_needs_realloc);'''
DIAG_FUNC_ANCHOR = 'static const char *TAG = "esp-aes";\n'


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def inputs(idf_path: Path) -> list[Path]:
    provenance = json.loads((PATCH_DIR / 'provenance.json').read_text())
    return [Path(__file__).resolve(), PATCH_DIR / 'provenance.json'] + [
        idf_path / name for name in provenance['idf_source_hashes_lf']]


def prepare(idf_path: Path, output_dir: Path, diagnostics: bool = False) -> Path:
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
    staged = result.decode()
    require(staged.count(PSRAM_BOUNCE_BEFORE) == 1 and staged.count(PSRAM_OUTPUT_ALLOC_BEFORE) == 1 and
            staged.count(PSRAM_LOOP_BEFORE) == 1, 'Expected exactly one reviewed AES bounce allocation setup')
    staged = staged.replace(PSRAM_BOUNCE_BEFORE, PSRAM_BOUNCE_AFTER, 1)
    staged = staged.replace(PSRAM_OUTPUT_ALLOC_BEFORE, PSRAM_OUTPUT_ALLOC_AFTER, 1)
    result = staged.replace(PSRAM_LOOP_BEFORE, PSRAM_LOOP_AFTER, 1).encode()
    require(hashlib.sha256(result).hexdigest() == provenance['generated_psram_bounce_sha256_lf'],
            'Generated AES PSRAM bounce source differs from reviewed patch')
    if diagnostics:
        diag = result.decode()
        # Instrument both the P4 branch and the ESP32-S3 branch of SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE.
        require(diag.count(DIAG_ANCHOR) == 1 and diag.count(DIAG_ANCHOR_S3) == 1 and
                diag.count(DIAG_FUNC_ANCHOR) == 1, 'AES diagnostic anchors are missing or duplicated')
        diag = diag.replace(DIAG_FUNC_ANCHOR, DIAG_FUNC_ANCHOR + DIAG_HELPER, 1)
        diag = diag.replace(DIAG_ANCHOR, DIAG_CALL, 1)
        result = diag.replace(DIAG_ANCHOR_S3, DIAG_CALL_S3, 1).encode()
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
    parser.add_argument('--diagnostics', action='store_true',
                        help='Release-test builds only: log AES bounce-buffer operand regions')
    args = parser.parse_args()
    try:
        if args.print_inputs:
            print('\n'.join(str(path) for path in inputs(args.idf_path)))
        else:
            output = prepare(args.idf_path, args.output_dir, args.diagnostics)
            print(f'AES DMA cleanup overlay verified: {output}' + (' (+diagnostics)' if args.diagnostics else ''))
    except (OSError, ValueError, UnicodeError) as error:
        print(f'AES DMA cleanup overlay refused: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
