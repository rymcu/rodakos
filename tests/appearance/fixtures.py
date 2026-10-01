import base64
import hashlib
import json
import pathlib
import sys
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

out = pathlib.Path(sys.argv[1])
key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
wrong = rsa.generate_private_key(public_exponent=65537, key_size=2048)
pem = key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode()
wrong_pem = wrong.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode()
der = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
key_id = hashlib.sha256(der).hexdigest()
payload = dict(schema='rodak-appearance-deployment-v1', deploymentId='deploy-1', deviceId='device-1',
               deviceKey='aa:bb:cc:dd:ee:ff', productKey='rymcu-bigsmart', revision=3, mode='custom',
               releaseId='release-1', packageSize=1234, packageSha256='a'*64, width=320, height=240, keyId=key_id)
def manifest(value, suffix=b''):
    encoded = json.dumps(value, separators=(',', ':')).encode() + suffix
    signature = key.sign(encoded, padding.PKCS1v15(), hashes.SHA256())
    return json.dumps(dict(signedPayload=base64.b64encode(encoded).decode(),
                          signature=base64.b64encode(signature).decode(), keyId=key_id), separators=(',', ':'))
constants = dict(kPem=pem, kWrongPem=wrong_pem, kKeyId=key_id, kManifest=manifest(payload))
for name, changes in [('kWrongDevice', dict(deviceKey='other-device')), ('kWrongProduct', dict(productKey='other-board')),
                       ('kOversize', dict(packageSize=524289)), ('kFraction', dict(revision=1.5)),
                       ('kBadSha', dict(packageSha256='f'*63)), ('kBadMode', dict(mode='executable'))]:
    constants[name] = manifest(dict(payload, **changes))
constants['kBuiltin'] = manifest(dict(payload, mode='builtin', releaseId='', packageSize=0, packageSha256=''))
constants['kTrailingPayload'] = manifest(payload, b'{}')
constants['kWhitespacePayload'] = manifest(payload, b'\n \t')
(out / 'fixtures.h').write_text('#pragma once\n' + '\n'.join('inline constexpr char '+name+'[] = '+json.dumps(value)+';' for name, value in constants.items()))
