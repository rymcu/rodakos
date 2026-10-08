"""Extract reviewed production functions verbatim; SDK dependencies are host fakes."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--idf-path', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools'))
from prepare_dvp_deinit_patch import SOURCE, function_text, prepare, verify

reviewed = verify(root / 'managed_components', args.idf_path)
generated = prepare(root / 'managed_components', args.output / 'overlay', args.idf_path)
functions = ['destroy_cam_device', 'init_dvp_clk_func', 'deinit_dvp_clk_func',
             'create_dvp_video_device', 'destroy_dvp_video_device',
             'esp_video_deinit_with_flags', 'esp_video_init_with_flags',
             'esp_video_init', 'esp_video_deinit']
manifest = {'mode': 'verbatim-production-functions-with-sdk-fakes', 'variants': {}}
for variant, source in [('upstream', reviewed[SOURCE].decode()),
                        ('patched', generated.read_text())]:
    pieces = []
    for name in functions:
        text = function_text(source, name)
        if variant == 'patched' and name == 'destroy_dvp_video_device':
            start = source.index('/* The existing s_init_lock owns this state')
            text = source[start:source.index(text) + len(text)]
        pieces.append(text)
    # This exact prefix owns the production flags, lock and internal init DTO.
    prefix = source[source.index('typedef esp_err_t (*esp_video_create_device_fn_t)'):
                    source.index('#if ESP_VIDEO_ENABLE_SCCB_DEVICE\nstatic esp_err_t destroy_cam_device')]
    content = '#include "host_fakes.h"\n#if ESP_VIDEO_ENABLE_SCCB_DEVICE\n' + prefix + '\n' + '\n\n'.join(pieces) + '\n'
    path = args.output / (variant + '.c')
    path.write_text(content, encoding='utf-8', newline='\n')
    manifest['variants'][variant] = {
        'completeSourceSha256Lf': hashlib.sha256(source.encode()).hexdigest(),
        'compiledFunctionsSha256': hashlib.sha256(content.encode()).hexdigest(),
        'functions': functions, 'completeTranslationUnit': False}
(args.output / 'production-sources.json').write_text(json.dumps(manifest, indent=2) + '\n')
