"""Check DVP RCC provenance, output boundaries and fail-closed drift handling."""
from pathlib import Path
import argparse
import json
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools'))
from prepare_dvp_rcc_patch import PATCH_DIR, SOURCE, prepare

parser = argparse.ArgumentParser()
parser.add_argument('--idf-path', type=Path, required=True)
args = parser.parse_args()
idf = args.idf_path.resolve()

for bad in (idf, idf / 'must-not-exist', root / 'managed_components' / 'must-not-exist'):
    try:
        prepare(idf, bad)
    except ValueError as error:
        assert 'outside' in str(error)
    else:
        raise AssertionError(f'Generator accepted forbidden output: {bad}')

with tempfile.TemporaryDirectory(prefix='rodak-dvp-rcc-generator-') as directory:
    base = Path(directory)
    idf_copy = base / 'idf'
    provenance = json.loads((PATCH_DIR / 'provenance.json').read_text())
    for name in provenance['idf_source_hashes_lf']:
        source_copy = idf_copy / name
        source_copy.parent.mkdir(parents=True, exist_ok=True)
        source_copy.write_bytes((idf / name).read_bytes())
    output = prepare(idf_copy, base / 'generated')
    original = output.read_bytes()
    source = idf_copy / SOURCE
    source.write_bytes(source.read_bytes() + b'\n/* unreviewed RCC drift */\n')
    try:
        prepare(idf_copy, base / 'generated')
    except ValueError as error:
        assert 'Unreviewed DVP RCC input' in str(error)
    else:
        raise AssertionError('Generator accepted changed IDF source')
    assert output.read_bytes() == original
print('RCC overlay rejects forbidden output and source drift; previous output is preserved')
