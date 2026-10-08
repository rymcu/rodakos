"""Extract the reviewed deinit function for host RCC tests."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools'))
from prepare_dvp_rcc_patch import SOURCE, function_text, prepare, verify

parser = argparse.ArgumentParser()
parser.add_argument('--idf-path', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()

reviewed = verify(args.idf_path)
generated_path = prepare(args.idf_path, args.output / 'overlay')
variants = {'upstream': reviewed[SOURCE].decode(), 'patched': generated_path.read_text()}
manifest = {'mode': 'verbatim-production-function-with-idf-rcc-macros', 'variants': {}}
for variant, source in variants.items():
    content = '#include "host_fakes.h"\n' + function_text(source, 'esp_cam_ctlr_dvp_deinit') + '\n'
    path = args.output / f'{variant}.c'
    args.output.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8', newline='\n')
    manifest['variants'][variant] = {
        'completeSourceSha256Lf': hashlib.sha256(source.encode()).hexdigest(),
        'compiledFunctionSha256': hashlib.sha256(content.encode()).hexdigest(),
        'function': 'esp_cam_ctlr_dvp_deinit',
        'completeTranslationUnit': False
    }
(args.output / 'production-sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
