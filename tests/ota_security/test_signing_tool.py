import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import ota_security as ota


class SigningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.key = rsa.generate_private_key(public_exponent=65537, key_size=2048)

    def setUp(self):
        self.manifest = dict(manifestVersion=2, taskNo="ota-1", version="v2", fileSize=3,
                             checksumType="sha256", checksumValue=hashlib.sha256(b"abc").hexdigest())
        self.manifest.update(signatureType=ota.ALGORITHM, signingKeyId=ota.key_id(self.key.public_key()))
        self.manifest["signatureValue"] = self.key.sign(
            ota.payload(self.manifest), padding.PKCS1v15(), hashes.SHA256()).hex()

    def test_valid_and_repeat(self):
        ota.verify(self.manifest, self.key.public_key())
        ota.verify(self.manifest, self.key.public_key())

    def test_each_bound_field(self):
        for field, value in (("taskNo", "other"), ("version", "v3"), ("fileSize", 4),
                             ("checksumValue", "a" * 64)):
            with self.subTest(field=field), self.assertRaises(InvalidSignature):
                m = copy.copy(self.manifest); m[field] = value
                ota.verify(m, self.key.public_key())

    def test_missing_bad_manifest(self):
        for field, value in (("manifestVersion", 1), ("manifestVersion", 2.5), ("manifestVersion", True),
                             ("fileSize", -1), ("fileSize", 1.5), ("fileSize", 0xd50001),
                             ("signatureValue", ""), ("signatureType", "none"),
                             ("taskNo", "a\nb"), ("version", "\x00"), ("checksumValue", "G" * 64)):
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                m = copy.copy(self.manifest); m[field] = value
                ota.verify(m, self.key.public_key())

    def test_image_tamper_and_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "image.bin"
            image.write_bytes(b"abc")
            ota.verify(self.manifest, self.key.public_key(), image)
            for data in (b"ab", b"abd", b"abcd"):
                image.write_bytes(data)
                with self.assertRaises(ValueError):
                    ota.verify(self.manifest, self.key.public_key(), image)

    def test_wrong_key(self):
        wrong = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        with self.assertRaises(ValueError):
            ota.verify(self.manifest, wrong.public_key())


if __name__ == "__main__":
    unittest.main()
