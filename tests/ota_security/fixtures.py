"""Generate ephemeral RSA fixtures, never a production trust root."""
import hashlib
import json
from pathlib import Path
import sys
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import ota_security as ota

directory = Path(sys.argv[1])
directory.mkdir(parents=True, exist_ok=True)
key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
wrong = rsa.generate_private_key(public_exponent=65537, key_size=2048)
image = b"test firmware" * 1000
manifest = dict(manifestVersion=2, taskNo="test-ota", version="1.0", fileSize=len(image),
                checksumType="sha256", checksumValue=hashlib.sha256(image).hexdigest())
signed = key.sign(ota.payload(manifest), padding.PKCS1v15(), hashes.SHA256()).hex()
wrong_signature = wrong.sign(ota.payload(manifest), padding.PKCS1v15(), hashes.SHA256()).hex()
(directory / "valid.bin").write_bytes(image)
(directory / "truncated.bin").write_bytes(image[:-1])
(directory / "tampered.bin").write_bytes(b"!" + image[1:])
(directory / "extra.bin").write_bytes(image + b"x")
(directory / "valid.sig").write_text(signed)
(directory / "short.sig").write_text(signed[:-1])
(directory / "extra.sig").write_text(signed + "\n")
pem = key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode()
(directory / "rodak_ota_trust.h").write_text(
    '#pragma once\ninline constexpr char kOtaPublicKeyPem[] = ' + json.dumps(pem) + ';\n'
    + 'inline constexpr char kOtaTrustMarker[] = "RODAK_OTA_TRUST:test-only";\n')
constants = dict(kSignature=signed, kWrongSignature=wrong_signature, kDigest=manifest["checksumValue"],
                 kFixtureDir=str(directory))
(directory / "fixtures.h").write_text("#pragma once\n" + "".join(
    f"inline constexpr char {name}[] = {json.dumps(value)};\n" for name, value in constants.items()))
