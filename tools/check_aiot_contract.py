#!/usr/bin/env python3
"""Validate the machine-readable Rodak AIoT v1 contract.

Checks:
  1. Every schema is a valid JSON Schema 2020-12 document.
  2. Every fixture in examples/valid/ matches its schema.
  3. Every fixture in examples/invalid/ is rejected by its schema.
  4. Every schema has at least one valid and one invalid fixture.
  5. openapi.yaml and asyncapi.yaml only reference existing schema files.
  6. Every contracts/... schema link in docs/rodak-aiot-contract-v1.md exists.
  7. Extension keywords for rules that JSON Schema cannot express (see EXTENSIONS below).
  8. The valid signed OTA manifest verifies with examples/ota-signing-public-key.pem, using the
     same signed text as Rodak and tools/ota_security.py (skipped if cryptography is absent).

A fixture is named <schema-name>.<case>.json. Requires: jsonschema>=4.18, referencing, PyYAML.
"""
from __future__ import annotations

import base64
import datetime
import json
import pathlib
import re
import sys

try:
    import yaml
    from jsonschema import Draft202012Validator
    from referencing import Registry, Resource
except ImportError as exc:  # pragma: no cover - environment message only
    sys.exit(f"missing dependency: {exc.name}. Install: pip install 'jsonschema>=4.18' PyYAML")

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONTRACT = ROOT / "contracts" / "rodak-aiot" / "v1"
SCHEMAS = CONTRACT / "schemas"
EXAMPLES = CONTRACT / "examples"
DOC = ROOT / "docs" / "rodak-aiot-contract-v1.md"


def load_json(path: pathlib.Path):
    return json.loads(path.read_text(encoding="utf-8"))


# EXTENSIONS — keywords this checker enforces in addition to JSON Schema:
#   x-maxUtf8Bytes: n            string length in UTF-8 bytes, counted like JavaScript TextEncoder
#   x-maxUtf16Units: {field: n}  string length in UTF-16 code units, like JavaScript String.length
#   x-equalFields: [[path, ...]] dotted paths that, when present, hold equal values
#   x-topicsUseKey: {...}        every device topic is devices/<key>/<suffix> with the object's own key;
#                                optional "lists" fixes the order of topic lists
#   x-effectVersion: true        desired frame: deviceEffect.shadowVersion == frame shadowVersion, and a
#                                light effectId embeds that version
#   x-selectedSecret: [fields]   Rodak's `a ?? b ?? c` choice: manual pairing needs it nonblank after
#                                JavaScript trim(); trust setup needs it empty
#   x-base64NonblankUtf8: n      base64 that decodes to UTF-8 text, nonblank after trim, at most n bytes
#   x-calendarDate: true         the date part of an ISO timestamp is a real calendar date
#   x-maxUtf16UnitsAfterTrim     with x-maxUtf16Units: measure after JavaScript trim(), as Rodak does
#   x-endpointMatches: {...}     endpoint URL == <mqtt|mqtts>://<host>:<port>, scheme from transport
#   x-samePresence: [[paths]]    the listed paths are either all present or all absent
#   x-forbiddenKeys: [keys]      no object at any depth has one of these keys
#   x-nonblankAfterTrim: true    string is nonempty after JavaScript trim()
#   x-noControlAfterTrim: true   after JavaScript trim(), no C0 control or DEL remains
#   (x-endpointMatches also checks sslEndpoint: "" without TLS, == endpoint with TLS)
JS_WHITESPACE = "\t\n\v\f\r              " \
                "    　﻿"


def js_trim(text: str) -> str:
    return text.strip(JS_WHITESPACE)


def utf8_bytes_like_js(text: str) -> int:
    # TextEncoder replaces a lone surrogate with U+FFFD, which is 3 bytes.
    return sum(3 if 0xD800 <= ord(c) <= 0xDFFF else len(c.encode("utf-8")) for c in text)


def utf16_units(text: str) -> int:
    return sum(2 if ord(c) > 0xFFFF else 1 for c in text)


def dig(value, dotted):
    node = value
    for part in dotted.split("."):
        if isinstance(node, dict) and part in node:
            node = node[part]
        else:
            return False, None
    return True, node


def extension_errors(schema, value, schemas, path="<root>"):
    """Yield messages for the EXTENSIONS keywords, following properties, combinators and local $refs."""
    if not isinstance(schema, dict):
        return
    ref = schema.get("$ref")
    if isinstance(ref, str) and ref.endswith(".schema.json"):
        target = schemas.get(ref.removesuffix(".schema.json"))
        if target is not None:
            yield from extension_errors(target, value, schemas, path)
    limit = schema.get("x-maxUtf8Bytes")
    if isinstance(limit, int) and isinstance(value, str) and utf8_bytes_like_js(value) > limit:
        yield f"{path}: {utf8_bytes_like_js(value)} UTF-8 bytes > {limit}"
    trim = js_trim if schema.get("x-maxUtf16UnitsAfterTrim") else (lambda text: text)
    for field, units in schema.get("x-maxUtf16Units", {}).items():
        if isinstance(value, dict) and isinstance(value.get(field), str) and \
                utf16_units(trim(value[field])) > units:
            yield f"{path}/{field}: {utf16_units(trim(value[field]))} UTF-16 units > {units}"
    rule = schema.get("x-endpointMatches")
    if rule and isinstance(value, dict):
        tls = value.get(rule["transport"]) == "mqtts"
        port = value.get(rule["port"])
        port_text = str(int(port)) if isinstance(port, (int, float)) and float(port).is_integer() else str(port)
        expected = f"{'mqtts' if tls else 'mqtt'}://{value.get(rule['host'])}:{port_text}"
        if value.get(rule["endpoint"]) != expected:
            yield f"{path}/{rule['endpoint']}: expected {expected}"
        ssl_key = rule.get("sslEndpoint")
        if ssl_key and ssl_key in value and value[ssl_key] != (expected if tls else ""):
            yield f"{path}/{ssl_key}: expected {expected if tls else 'empty string'}"
    for group in schema.get("x-samePresence", []):
        present = {dotted: dig(value, dotted)[0] for dotted in group}
        if len(set(present.values())) > 1:
            yield f"{path}: {present} must be all present or all absent"
    if schema.get("x-nonblankAfterTrim") and isinstance(value, str) and not js_trim(value):
        yield f"{path}: blank after trim"
    if schema.get("x-noControlAfterTrim") and isinstance(value, str) and \
            any(ord(ch) < 0x20 or ord(ch) == 0x7F for ch in js_trim(value)):
        yield f"{path}: control character after trim"
    forbidden = set(schema.get("x-forbiddenKeys", []))
    if forbidden:
        stack = [value]
        while stack:
            node = stack.pop()
            if isinstance(node, dict):
                hit = forbidden.intersection(node)
                if hit:
                    yield f"{path}: forbidden key {sorted(hit)}"
                stack.extend(node.values())
            elif isinstance(node, list):
                stack.extend(node)
    for group in schema.get("x-equalFields", []):
        values = [(dotted, node) for dotted in group for found, node in [dig(value, dotted)] if found]
        if len({json.dumps(v, sort_keys=True) for _, v in values}) > 1:
            yield f"{path}: fields {[d for d, _ in values]} differ"
    rule = schema.get("x-topicsUseKey")
    if rule and isinstance(value, dict):
        key, topics = value.get(rule["key"]), value.get(rule["topics"], {})
        for name, suffix in rule["suffixes"].items():
            if isinstance(topics, dict) and name in topics and topics[name] != f"devices/{key}/{suffix}":
                yield f"{path}/{rule['topics']}/{name}: expected devices/{key}/{suffix}"
        for list_name, members in rule.get("lists", {}).items():
            expected = [f"devices/{key}/{rule['suffixes'].get(m, m)}" if m in rule["suffixes"]
                        else f"devices/{key}/{m}" for m in members]
            if list_name in value and value[list_name] != expected:
                yield f"{path}/{list_name}: expected {expected}"
    if schema.get("x-effectVersion") and isinstance(value, dict):
        effect = value.get("_meta", {}).get("rodak/deviceEffect") if isinstance(value.get("_meta"), dict) else None
        if isinstance(effect, dict) and isinstance(value.get("shadowVersion"), int):
            if effect.get("shadowVersion") != value["shadowVersion"]:
                yield f"{path}: deviceEffect.shadowVersion must equal the frame shadowVersion"
            effect_id = effect.get("effectId", "")
            if isinstance(effect_id, str) and effect_id.startswith("light:") and \
                    effect_id.split(":")[1] != str(value["shadowVersion"]):
                yield f"{path}: light effectId must embed the frame shadowVersion"
    fields = schema.get("x-selectedSecret")
    if fields and isinstance(value, dict):
        selected = next((value[f] for f in fields if value.get(f) is not None), "")
        text = str(selected)
        if "trustSetup" in value:
            if text:
                yield f"{path}: trust setup must not carry a secret"
        elif not js_trim(text):
            yield f"{path}: the selected secret is blank"
    limit = schema.get("x-base64NonblankUtf8")
    if isinstance(limit, int) and isinstance(value, str):
        try:
            raw = base64.b64decode(value, validate=True)
            text = raw.decode("utf-8")
        except (ValueError, UnicodeDecodeError):
            yield f"{path}: not base64 UTF-8"
        else:
            if not js_trim(text):
                yield f"{path}: decodes to blank text"
            if len(raw) > limit:
                yield f"{path}: decodes to {len(raw)} bytes > {limit}"
    if schema.get("x-calendarDate") and isinstance(value, str) and len(value) >= 10:
        try:
            datetime.date.fromisoformat(value[:10])
        except ValueError:
            yield f"{path}: {value[:10]} is not a calendar date"
    for key in ("allOf", "anyOf", "oneOf"):
        for sub in schema.get(key, []):
            yield from extension_errors(sub, value, schemas, path)
    for key in ("then", "else"):
        if key in schema:
            yield from extension_errors(schema[key], value, schemas, path)
    if isinstance(value, dict):
        for name, sub in schema.get("properties", {}).items():
            if name in value:
                yield from extension_errors(sub, value[name], schemas, f"{path}/{name}")


def main() -> int:
    errors: list[str] = []
    schemas = {p.name.removesuffix(".schema.json"): load_json(p) for p in sorted(SCHEMAS.glob("*.schema.json"))}
    if not schemas:
        print(f"no schemas under {SCHEMAS}")
        return 1

    registry = Registry()
    for name, schema in schemas.items():
        resource = Resource.from_contents(schema)
        registry = registry.with_resource(schema["$id"], resource)
        registry = registry.with_resource(f"{name}.schema.json", resource)

    validators = {}
    for name, schema in schemas.items():
        try:
            Draft202012Validator.check_schema(schema)
        except Exception as exc:  # noqa: BLE001 - report every schema defect
            errors.append(f"schema {name}: {exc}")
            continue
        validators[name] = Draft202012Validator(schema, registry=registry)

    coverage = {name: {"valid": 0, "invalid": 0} for name in schemas}
    for kind in ("valid", "invalid"):
        for fixture in sorted((EXAMPLES / kind).glob("*.json")):
            schema_name = fixture.name.split(".", 1)[0]
            validator = validators.get(schema_name)
            if validator is None:
                errors.append(f"{kind}/{fixture.name}: no schema named {schema_name}")
                continue
            instance = load_json(fixture)
            failures = sorted(validator.iter_errors(instance), key=lambda e: list(e.path))
            failures += list(extension_errors(schemas[schema_name], instance, schemas))
            if kind == "valid" and failures:
                first = failures[0]
                if isinstance(first, str):
                    errors.append(f"valid/{fixture.name}: rejected: {first}")
                else:
                    where = "/".join(str(p) for p in first.absolute_path) or "<root>"
                    errors.append(f"valid/{fixture.name}: rejected at {where}: {first.message}")
            elif kind == "invalid" and not failures:
                errors.append(f"invalid/{fixture.name}: accepted, but it must be rejected")
            coverage[schema_name][kind] += 1

    for name, counts in coverage.items():
        for kind, count in counts.items():
            if count == 0:
                errors.append(f"schema {name}: no {kind} fixture")

    for spec in ("openapi.yaml", "asyncapi.yaml"):
        text = (CONTRACT / spec).read_text(encoding="utf-8")
        yaml.safe_load(text)
        for ref in sorted(set(re.findall(r"schemas/([a-z0-9-]+)\.schema\.json", text))):
            if ref not in schemas:
                errors.append(f"{spec}: references missing schema {ref}")

    for ref in sorted(set(re.findall(r"contracts/rodak-aiot/v1/([A-Za-z0-9_./-]+)", DOC.read_text(encoding="utf-8")))):
        target = CONTRACT / ref.rstrip(").")
        if not target.exists():
            errors.append(f"{DOC.name}: link to missing {ref}")

    manifest = EXAMPLES / "valid" / "ota-manifest.signed-v2.json"
    public_key = EXAMPLES / "ota-signing-public-key.pem"
    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import padding
    except ImportError:
        print("note: cryptography not installed; OTA signature fixture not verified")
    else:
        m = load_json(manifest)
        fields = ["rodakos-ota-v2", "rymcu-bigsmart", "ota_0", "rsa2048-sha256", m["taskNo"], m["version"],
                  str(m["fileSize"]), m["checksumValue"], ""]
        signed = "\n".join(fields).encode("utf-8")
        key = serialization.load_pem_public_key(public_key.read_bytes())
        try:
            key.verify(bytes.fromhex(m["signatureValue"]), signed, padding.PKCS1v15(), hashes.SHA256())
        except Exception:  # noqa: BLE001
            errors.append(f"valid/{manifest.name}: signature does not verify")

    total = sum(c["valid"] + c["invalid"] for c in coverage.values())
    if errors:
        print("\n".join(errors))
        print(f"FAILED: {len(errors)} problem(s), {len(schemas)} schemas, {total} fixtures")
        return 1
    print(f"OK: {len(schemas)} schemas, {total} fixtures, openapi/asyncapi references resolved")
    return 0


if __name__ == "__main__":
    sys.exit(main())
