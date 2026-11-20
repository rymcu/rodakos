# Rodak AIoT v1 machine-readable contract

Machine-readable form of [Rodak AIoT v1](../../../docs/rodak-aiot-contract-v1.md). The prose
document is normative. These files encode its wire shapes so that both repositories can test
against one source.

| Path | Content |
| --- | --- |
| `openapi.yaml` | OpenAPI 3.1: device HTTP operations (contract §4, §12). |
| `asyncapi.yaml` | AsyncAPI 3.0: MQTT channels, direction, QoS (contract §7.3). |
| `schemas/*.schema.json` | JSON Schema 2020-12, one per payload, canonical field names. |
| `examples/valid/` | Payloads that the schema MUST accept. |
| `examples/invalid/` | Payloads that the schema MUST reject. |

A fixture is named `<schema-name>.<case>.json`. Each schema needs at least one valid and one
invalid fixture.

## Scope

The Rodak server's behavior is the source of truth. Each schema states its profile:

| Direction | Schema describes |
| --- | --- |
| Rodak → device | What Rodak really emits, field for field. |
| Device → Rodak | What RodakOS emits. The description says where Rodak accepts more. |

- RodakOS also accepts older aliases (contract §7.2). Those aliases are not in the schemas.
- Schemas check structure. Live correlation (effect receipts, exact Stop, OTA task ownership) and
  behavioral rules (§5 state machine, §6 transactions, §8.5 replay) stay in the prose.
- The valid fixtures share one device (`aa:bb:cc:dd:ee:ff`), one OTA task and Rodak's real ID
  formats. Each binding request ID is a separate scenario (manual, trust setup, expired, revoked).
  A few fixtures are standalone edge cases and are named for the rule they show (for example
  `binding-request.trimmed-key-at-128-units`). The access token is an HS256 JWT signed with a fixture-only key.
  `ota-manifest.signed-v2.json` carries a real RSA signature that verifies with
  `examples/ota-signing-public-key.pem` (test key, never trusted by firmware).

## Extension keywords

Some Rodak rules cannot be written in JSON Schema. The schemas state them with `x-` keywords, and
`tools/check_aiot_contract.py` enforces them (see the `EXTENSIONS` comment there):
UTF-8 and UTF-16 length limits as JavaScript counts them, equal fields across the token response
and both MQTT descriptors, device topics built from the descriptor's own device key, endpoint URL
matching host, port and transport, effect version matching the frame version, Rodak's `??` secret
selection, base64 signal text that is not blank, calendar-valid timestamps, and keys forbidden at
any depth. A generic JSON Schema validator ignores these keywords. Use the checker.

## Known limits

These are deliberate. They do not affect a normal integration path:

- OpenAPI bodies are not run through the checker; only `schemas/` and `examples/` are.
- Host names in `servers` (`rodak.local`, `rodak-a1b2c3d4e5f60718.local`) are examples. Rodak uses
  its advertise host or a local IPv4 address, and derives the TLS name from its fingerprint.
- `announceSeq` is capped at 2^53−1 (a JSON safe integer); Rodak's counter has no cap.
- Base64 checks accept non-canonical padding bits (for example `YR==`).
- Live correlation (effect receipt against the pending dispatch, exact Stop, OTA task ownership,
  token validity) needs server state and stays in the prose.

## Check

```sh
python3 -m venv build/contract-venv
build/contract-venv/bin/python -m pip install "jsonschema==4.23.0" "PyYAML==6.0.2" "cryptography==44.0.0"
build/contract-venv/bin/python -I tools/check_aiot_contract.py
```

`cryptography` is optional; without it the OTA signature check is skipped.

The firmware host test `tests/app_model/contract_fixture_test.cc` parses the binding fixtures with
the production `ParseDevicePairingResponse`, and the `realtimeVoice` of Rodak's bootstrap and token
responses with `ParseRealtimeVoiceDescriptor`.

Rodak validates its real responses against these schemas and maintains the authoritative copy;
see [contracts/README.md](../../README.md).

## Change rule

When Rodak changes a wire shape, update in one commit: the prose contract, the schema, at least one
valid and one invalid fixture, and the contract changelog (§15). The fixtures were generated once
and are now edited by hand.
