# Production OTA Signing

This procedure keeps the production private key on an offline signing host. The build host, the
repository, firmware images, OTA packages and the Rodak server receive only the public key.
Development test keys under `build/test-keys/` never qualify a release.

## Roles

| Host | Holds | Does |
| ---- | ----- | ---- |
| Offline signing host | RSA-2048 private key | Generates the key pair once and signs one payload per Rodak task |
| Build host | Public key PEM | Builds main and Recovery, packages, writes signing requests, attaches signatures |
| Rodak | Nothing secret | Accepts the signed manifest per task through `server:attachOtaTaskSignature` |

## 1. Generate the key pair (offline host, once)

```sh
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out rodak-ota-release-private.pem
openssl pkey -in rodak-ota-release-private.pem -pubout -out rodak-ota-release-public.pem
openssl pkey -pubin -in rodak-ota-release-public.pem -outform DER | openssl dgst -sha256
```

The last line prints the key fingerprint. Firmware embeds it as `RODAK_OTA_TRUST:<fingerprint>`, and
signed manifests carry it as `signingKeyId`. Record the fingerprint with the release. Move only
`rodak-ota-release-public.pem` to the build host. Keep a sealed backup of the private key. If you lose
it, every installed Recovery needs a new wired migration.

## 2. Build with the public key (build host)

Configure both projects with the production public key and package without any private key:

```powershell
.\build_ota_bundle.ps1 `
  -VerificationKeyPath C:\secure\rodak-ota-release-public.pem `
  -SigningTaskNo <rodak-taskNo> `
  -SigningVersion <compiled-version> `
  -OfflineSigning
```

`-OfflineSigning` refuses a `-SigningKeyPath`. It writes `signing-request.json` next to the package
and stops before `verify-package`, so the unsigned package cannot be flashed or uploaded by mistake.
The request contains the exact UTF-8 `payload` that the device verifies:

```text
rodakos-ota-v2
rymcu-bigsmart
ota_0
rsa2048-sha256
<taskNo>
<version>
<fileSize>
<lowercase SHA-256>
```

A changed production key changes the trust anchor in Recovery. Recovery is immutable for normal
OTA, so the first production build needs a wired Recovery migration: back up the full flash, flash
the merged image, then read back and compare (step 5).

## 3. Sign one payload (offline host)

Copy only `signing-request.json` to the offline host. Write its `payload` field byte for byte, with
no added newline conversion, and sign it:

```sh
python3 -c "import json;open('payload.txt','w',newline='').write(json.load(open('signing-request.json'))['payload'])"
openssl dgst -sha256 -sign rodak-ota-release-private.pem -out signature.bin payload.txt
python3 -c "print(open('signature.bin','rb').read().hex(), end='')" > signature.hex
```

Before you sign, make sure that the `taskNo`, `version`, `fileSize` and `checksumValue` in the
request match the intended release. Copy only `signature.hex` back.

## 4. Attach and verify (build host)

```powershell
python tools/ota_security.py attach-signature --manifest <pkg>\manifest.json `
  --public-key C:\secure\rodak-ota-release-public.pem --image <pkg>\rodakos.bin `
  --task <rodak-taskNo> --version <compiled-version> --signature signature.hex
python tools/ota_security.py verify-package --directory <pkg>
```

`attach-signature` verifies the signature against the public key before it writes the manifest. A
signature for a different task, version, image or key fails. For a Rodak OTA, attach the same
signed manifest with `server:attachOtaTaskSignature` and pass the public key so Rodak verifies it
too. A Rodak retry creates a new `taskNo` and needs a new signature.

## 5. Read back after a wired flash

After a wired migration, read the installed regions back and compare them with the package:

```powershell
python -m esptool --chip esp32s3 -p COM3 read-flash 0x20000 0x280000 recovery-readback.bin
python -m esptool --chip esp32s3 -p COM3 read-flash 0x2a0000 0xd50000 app-readback.bin
```

The first `rodakos_recovery.bin`-sized bytes of `recovery-readback.bin` and the first
`rodakos.bin`-sized bytes of `app-readback.bin` must match the package files, and both images must
contain `RODAK_OTA_TRUST:<production fingerprint>`. `flash_and_test.ps1 -VerifyOnly` must then pass.

## Status

The offline tooling and the procedure exist. Nobody has generated or deployed a production key yet, so
the production-root gate in [OTA release readiness](ota-release-readiness.md) stays open until a
device runs a production-signed Recovery and main image, passes readback, and completes a Rodak OTA
signed on the offline host.
