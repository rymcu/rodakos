"""Sign OTA manifests and generate the firmware's public trust anchor."""

import argparse
import hashlib
import json
from pathlib import Path
import re

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

ALGORITHM = "rsa2048-sha256"
PREFIX = "rodakos-ota-v2\nrymcu-bigsmart\nota_0\nrsa2048-sha256\n"
FAULT_MARKER = b"RODAKOS_RELEASE_FAULT_INJECTION_ACTIVE"


def public_key(path):
    key = serialization.load_pem_public_key(Path(path).read_bytes())
    if not isinstance(key, rsa.RSAPublicKey) or key.key_size != 2048:
        raise ValueError("Verification key must be an RSA-2048 public key")
    return key


def key_id(key):
    return hashlib.sha256(key.public_bytes(
        serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)).hexdigest()


def payload(manifest):
    if type(manifest.get("manifestVersion")) is not int or manifest["manifestVersion"] != 2:
        raise ValueError("manifestVersion must equal 2")
    for field, limit in (("taskNo", 95), ("version", 127)):
        value = manifest.get(field)
        if not isinstance(value, str) or not 0 < len(value.encode("utf-8")) <= limit:
            raise ValueError(f"Invalid {field}")
        if any(ord(ch) < 32 or ord(ch) == 127 for ch in value):
            raise ValueError(f"Control character in {field}")
    size = manifest.get("fileSize")
    if type(size) is not int or not 0 < size <= 0xD50000:
        raise ValueError("Invalid fileSize")
    if manifest.get("checksumType") != "sha256" or not re.fullmatch(
            "[0-9a-f]{64}", manifest.get("checksumValue", "")):
        raise ValueError("Invalid SHA-256 checksum")
    return (PREFIX + f'{manifest["taskNo"]}\n{manifest["version"]}\n{size}\n'
            + manifest["checksumValue"] + "\n").encode("utf-8")


def check_image(manifest, path):
    data = Path(path).read_bytes()
    if len(data) != manifest["fileSize"] or hashlib.sha256(data).hexdigest() != manifest["checksumValue"]:
        raise ValueError("Image size or SHA-256 does not match manifest")


def verify(manifest, key, image=None):
    data = payload(manifest)
    if manifest.get("signatureType") != ALGORITHM or not re.fullmatch(
            "[0-9a-f]{512}", manifest.get("signatureValue", "")):
        raise ValueError("Missing or malformed signature")
    if manifest.get("signingKeyId") != key_id(key):
        raise ValueError("Signing key does not match verification key")
    key.verify(bytes.fromhex(manifest["signatureValue"]), data, padding.PKCS1v15(), hashes.SHA256())
    if image:
        check_image(manifest, image)


def write_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".part")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    header = commands.add_parser("key-header")
    header.add_argument("--public-key", default="")
    header.add_argument("--output", required=True)
    for action in ("sign", "verify"):
        sub = commands.add_parser(action)
        sub.add_argument("--manifest", required=True)
        sub.add_argument("--public-key", required=True)
        sub.add_argument("--image", required=True)
        if action == "sign":
            sub.add_argument("--private-key", required=True)
            sub.add_argument("--task", required=True)
            sub.add_argument("--version", required=True)
    package = commands.add_parser("verify-package")
    package.add_argument("--directory", required=True)
    package.add_argument("--allow-faults", action="store_true")
    args = parser.parse_args()
    if args.command == "key-header":
        key = public_key(args.public_key) if args.public_key else None
        pem = key.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode() if key else ""
        marker = "RODAK_OTA_TRUST:" + (key_id(key) if key else "unconfigured")
        Path(args.output).write_text(
            '#pragma once\ninline constexpr char kOtaPublicKeyPem[] = ' + json.dumps(pem) + ';\n'
            + 'inline constexpr char kOtaTrustMarker[] = ' + json.dumps(marker) + ';\n', encoding="utf-8")
        return
    if args.command == "verify-package":
        directory = Path(args.directory)
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8-sig"))
        name = manifest["fileName"]
        if Path(name).name != name:
            raise ValueError("Package image must be a basename")
        key = public_key(directory / "ota-public.pem")
        verify(manifest, key, directory / name)
        if manifest.get("otaJournalSchemaVersion") != 1:
            raise ValueError("Unsupported Recovery journal ABI")
        image = (directory / name).read_bytes()
        # esp_app_desc_t follows the 24-byte image header and first 8-byte segment header.
        if len(image) < 80 or image[0] != 0xe9 or int.from_bytes(image[32:36], "little") != 0xabcd5432:
            raise ValueError("Missing ESP application descriptor")
        compiled_version = image[48:80].split(b"\0", 1)[0].decode("utf-8")
        if compiled_version != manifest["version"]:
            raise ValueError("Signed version does not match embedded application descriptor")
        marker = ("RODAK_OTA_TRUST:" + key_id(key)).encode()
        fault_build = False
        for file in (name, "rodakos_recovery.bin"):
            data = (directory / file).read_bytes()
            if marker not in data:
                raise ValueError(f"Unmatched trust anchor: {file}")
            fault_build = fault_build or FAULT_MARKER in data
        if fault_build != manifest.get("releaseFaultInjection", False):
            raise ValueError("Binary and manifest fault-injection flavor disagree")
        if fault_build and not (args.allow_faults and manifest.get("developmentPackage") is True
                                and manifest.get("buildFlavor") == "release-fault-test"):
            raise ValueError("Fault-injection packages require an explicit development test opt-in")
        return
    manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8-sig"))
    key = public_key(args.public_key)
    if args.command == "sign":
        private = serialization.load_pem_private_key(Path(args.private_key).read_bytes(), password=None)
        if not isinstance(private, rsa.RSAPrivateKey) or private.key_size != 2048 or key_id(private.public_key()) != key_id(key):
            raise ValueError("Private key must match the configured RSA-2048 trust anchor")
        manifest.update(taskNo=args.task, version=args.version)
        check_image(manifest, args.image)
        signature = private.sign(payload(manifest), padding.PKCS1v15(), hashes.SHA256())
        manifest.update(signatureType=ALGORITHM, signatureValue=signature.hex(), signingKeyId=key_id(key))
        verify(manifest, key, args.image)
        write_json(args.manifest, manifest)
    else:
        verify(manifest, key, args.image)


if __name__ == "__main__":
    main()
