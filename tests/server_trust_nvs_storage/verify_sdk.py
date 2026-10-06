"""Check the complete reviewed SDK storage/CRC sources before compiling the real NVS engine."""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def verify(idf_path: Path) -> int:
    provenance = json.loads(Path(__file__).with_name("sdk-provenance.json").read_text())
    for relative, expected in provenance["source_hashes_lf"].items():
        content = (idf_path / relative).read_bytes().replace(b"\r\n", b"\n")
        actual = hashlib.sha256(content).hexdigest()
        if actual != expected:
            raise ValueError(f"Unreviewed SDK source: {relative} (SHA-256 {actual})")
    return len(provenance["source_hashes_lf"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf-path", required=True, type=Path)
    args = parser.parse_args()
    try:
        count = verify(args.idf_path)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
    print(f"Reviewed ESP-IDF 6.0.2 NVS storage sources verified: {count} files")
