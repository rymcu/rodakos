"""Check fail-closed hashes and refusal to write inside managed dependencies."""
from pathlib import Path
import argparse
import json
import tempfile
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools'))
from prepare_dvp_deinit_patch import PATCH, SOURCE, prepare
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--idf-path', type=Path, required=True)
args = parser.parse_args()

managed = root / 'managed_components'
try:
    prepare(managed, managed / 'must-not-exist', args.idf_path)
except ValueError as error:
    assert 'outside managed' in str(error)
else:
    raise AssertionError('Generator accepted managed output')
with tempfile.TemporaryDirectory(prefix='rodak-dvp-generator-') as directory:
    base = Path(directory)
    copy = base / 'managed'
    provenance = json.loads((PATCH / 'provenance.json').read_text())
    for name in provenance['managed_hashes_lf']:
        path = copy / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((managed / name).read_bytes())
    output = prepare(copy, base / 'generated', args.idf_path)
    original = output.read_bytes()
    source = copy / SOURCE
    source.write_bytes(source.read_bytes() + b'\n/* unreviewed */\n')
    try:
        prepare(copy, base / 'generated', args.idf_path)
    except ValueError as error:
        assert 'Unreviewed DVP dependency' in str(error)
    else:
        raise AssertionError('Generator accepted changed dependency')
    assert output.read_bytes() == original
print('Managed output and unreviewed dependency rejected; prior generated output unchanged')
