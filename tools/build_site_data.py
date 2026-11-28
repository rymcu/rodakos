from pathlib import Path
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
contracts = ROOT / "contracts"
proto_root = ROOT / "site" / "public" / "protocols"
out = proto_root / "data" / "fixtures.json"
items = []
for examples_root in sorted(contracts.glob("*/v1/examples")):
    contract = examples_root.parent.parent.name
    version = examples_root.parent.name
    for kind in ("valid", "invalid"):
        folder = examples_root / kind
        if not folder.exists():
            continue
        for path in sorted(folder.iterdir()):
            if not path.is_file():
                continue
            raw = path.read_text(encoding="utf-8")
            try:
                content = json.loads(raw) if path.suffix.lower() == ".json" else raw
            except json.JSONDecodeError:
                content = raw
            stem = path.name.split(".")[0]
            schema_path = examples_root.parent / "schemas" / f"{stem}.schema.json"
            items.append({
                "contract": f"{contract}/{version}",
                "kind": kind,
                "schema": f"{stem}.schema.json" if schema_path.exists() else None,
                "filename": path.name,
                "content": content,
            })
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(items, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

key_src = contracts / "rodak-aiot" / "v1" / "examples" / "ota-signing-public-key.pem"
if key_src.exists():
    # Test key only; the OTA lab page verifies the fixture signature with it in the browser.
    shutil.copyfile(key_src, out.parent / key_src.name)

print(f"Wrote {len(items)} fixtures to {out}")
