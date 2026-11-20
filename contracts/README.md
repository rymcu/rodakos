# Rodak machine-readable contracts (copy)

**Source of truth: the Rodak repository, `rymcu/rodak` `contracts/`.** Rodak's integration tests load
these schemas and validate real server responses
(`tests/main-integration/aiot-contract-conformance.test.ts`,
`realtime-voice-contract-conformance.test.ts`, `contract-fixtures.test.ts`).

This directory is a byte-identical copy used by RodakOS host tests (`tests/app_model/*_fixture_test.cc`)
and `tools/check_aiot_contract.py`.

| Directory | Contract |
| --- | --- |
| `rodak-aiot/v1/` | [Rodak AIoT v1](../docs/rodak-aiot-contract-v1.md) |
| `rodak-realtime-voice/v1/` | [Rodak realtime voice v1](../docs/rodak-realtime-voice-contract-v1.md) |

## Sync rule

- Change a schema or fixture in Rodak first, with the code change it describes.
- Copy the changed files here unchanged (LF line endings) and run
  `python3 -I tools/check_aiot_contract.py` and the `tests/app_model` host tests.
- `x-` extension keywords have two implementations that must agree: Rodak
  `tests/main/utils/contract-validator.ts` (ajv) and `tools/check_aiot_contract.py` (Python).
  Add a new keyword to both, with a fixture that only the new rule rejects.
