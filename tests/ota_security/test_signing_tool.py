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


class OfflineSigningTests(unittest.TestCase):
    """The build host holds only the public key; an offline host signs the request payload."""

    @classmethod
    def setUpClass(cls):
        cls.key = rsa.generate_private_key(public_exponent=65537, key_size=2048)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.image = Path(self.directory.name) / "rodakos.bin"
        self.image.write_bytes(b"firmware" * 100)
        data = self.image.read_bytes()
        self.manifest = dict(manifestVersion=2, fileName="rodakos.bin", fileSize=len(data),
                             checksumType="sha256", checksumValue=hashlib.sha256(data).hexdigest())

    def tearDown(self):
        self.directory.cleanup()

    def offline_sign(self, request, key=None):
        return (key or self.key).sign(request["payload"].encode("utf-8"),
                                      padding.PKCS1v15(), hashes.SHA256()).hex()

    def test_round_trip_matches_direct_signing(self):
        request = ota.signing_request(self.manifest, self.image, "ota_1_abc", "1.2.3")
        self.assertEqual(request["payload"].encode("utf-8"),
                         ota.payload(dict(request, payload=None)))
        signed = ota.attach_signature(self.manifest, self.key.public_key(), "ota_1_abc", "1.2.3",
                                      self.offline_sign(request) + "\n", self.image)
        ota.verify(signed, self.key.public_key(), self.image)
        self.assertEqual(signed["taskNo"], "ota_1_abc")
        self.assertEqual(signed["signingKeyId"], ota.key_id(self.key.public_key()))
        self.assertEqual(signed["fileName"], "rodakos.bin")

    def test_request_rejects_image_that_differs_from_manifest(self):
        self.image.write_bytes(b"tampered")
        with self.assertRaises(ValueError):
            ota.signing_request(self.manifest, self.image, "ota_1_abc", "1.2.3")

    def test_attach_rejects_signature_for_other_task_or_key(self):
        request = ota.signing_request(self.manifest, self.image, "ota_1_abc", "1.2.3")
        with self.assertRaises(InvalidSignature):
            ota.attach_signature(self.manifest, self.key.public_key(), "ota_2_def", "1.2.3",
                                 self.offline_sign(request), self.image)
        wrong = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        with self.assertRaises(InvalidSignature):
            ota.attach_signature(self.manifest, self.key.public_key(), "ota_1_abc", "1.2.3",
                                 self.offline_sign(request, wrong), self.image)
        with self.assertRaises(ValueError):
            ota.attach_signature(self.manifest, self.key.public_key(), "ota_1_abc", "1.2.3",
                                 "zz" * 256, self.image)


if __name__ == "__main__":
    unittest.main()
