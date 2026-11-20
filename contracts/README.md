# Rodak machine-readable contracts

Machine-readable form of the Rodak ↔ RodakOS wire contracts. Rodak maintains the authoritative copy
and validates real server responses against it; this repository mirrors it byte for byte.

| Directory | Contract |
| --- | --- |
| `rodak-aiot/v1/` | [Rodak AIoT v1](../docs/rodak-aiot-contract-v1.md) |
| `rodak-realtime-voice/v1/` | [Rodak realtime voice v1](../docs/rodak-realtime-voice-contract-v1.md) |

In this repository the files feed `tools/check_aiot_contract.py` and the firmware host tests
`tests/app_model/contract_fixture_test.cc` and `voice_contract_fixture_test.cc`.

## Sync rule

- A wire change is made on the server side first, together with its schema and fixtures.
- The changed files are copied here unchanged (LF line endings). Then run
  `python3 -I tools/check_aiot_contract.py` and the `tests/app_model` host tests.
- `x-` extension keywords are implemented twice (the server's validator and
  `tools/check_aiot_contract.py`). A new keyword is added to both, with a fixture that only the new
  rule rejects.
- Text in these files describes wire behavior only: fields, values, codes, topics, timings.
  `tools/check_aiot_contract.py` rejects references to server source code.
