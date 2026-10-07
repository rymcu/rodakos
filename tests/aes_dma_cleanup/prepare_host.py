"""Copy complete AES production translation units; substitute only SDK headers/APIs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--idf-path', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--variant', choices=('baseline', 'patched'), required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
args.output_dir.mkdir(parents=True, exist_ok=True)
sources = {'esp_aes_dma_core.c': 'components/mbedtls/port/aes/dma/esp_aes_dma_core.c',
           'esp_aes.c': 'components/mbedtls/port/aes/esp_aes.c',
           'esp_aes_common.c': 'components/mbedtls/port/aes/esp_aes_common.c'}
manifest = {'variant': args.variant, 'translationUnits': {}}
if args.variant == 'patched':
    sys.path.insert(0, str(root / 'tools'))
    from prepare_aes_dma_cleanup_patch import prepare
    prepare(args.idf_path, args.output_dir)
else:
    provenance = json.loads((root / 'patches/aes_dma_cleanup/6.0.2/provenance.json').read_text())
    for name, expected in provenance['idf_source_hashes_lf'].items():
        actual = hashlib.sha256((args.idf_path / name).read_bytes().replace(b'\r\n', b'\n')).hexdigest()
        if actual != expected:
            raise ValueError(f'Unreviewed baseline IDF source: {name}')
headers = set()
for filename, relative in sources.items():
    raw = (args.idf_path / relative).read_bytes().replace(b'\r\n', b'\n')
    source = args.output_dir / filename
    if args.variant == 'baseline' or filename != 'esp_aes_dma_core.c':
        source.write_bytes(raw)
    content = source.read_bytes()
    manifest['translationUnits'][filename] = {'upstreamSha256Lf': hashlib.sha256(raw).hexdigest(),
                                             'compiledSha256': hashlib.sha256(content).hexdigest(),
                                             'completeTranslationUnit': True}
    headers.update(re.findall(r'^#include "([^"]+)"', content.decode(), re.M))
headers.update(('esp_types.h', 'hal/aes_types.h', 'freertos/FreeRTOS.h', 'freertos/semphr.h'))
for name in sorted(headers):
    path = args.output_dir / 'fakes' / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('/* SDK dependency replaced by host_fakes.h; not production code. */\n')
public = args.idf_path / 'components/mbedtls/port/include/aes/esp_aes.h'
path = args.output_dir / 'real/aes/esp_aes.h'
path.parent.mkdir(parents=True, exist_ok=True)
path.write_bytes(public.read_bytes())
manifest['contextAndPublicApiHeaderSha256Lf'] = hashlib.sha256(public.read_bytes().replace(b'\r\n', b'\n')).hexdigest()
(args.output_dir / 'production-sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
