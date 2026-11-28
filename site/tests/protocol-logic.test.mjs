import assert from 'node:assert/strict'
import fs from 'node:fs'
import crypto from 'node:crypto'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import {
  parseRav1,
  classifyEnvelope,
  transitionBinding,
  allowedBindingEvents,
  gateMessage,
  submitCommand,
  emptyLedger,
  REPLAY_WINDOW,
  buildSignatureString
} from '../shared/protocol-logic.mjs'

const here = path.dirname(fileURLToPath(import.meta.url))
const fixtures = JSON.parse(
  fs.readFileSync(path.join(here, '../public/protocols/data/fixtures.json'), 'utf8')
)
const fixture = (name) => fixtures.find((item) => item.filename === name).content

// RAV1: every contract fixture parses to its expected verdict.
for (const item of fixtures.filter((x) => x.schema === 'rav1-frame.schema.json')) {
  assert.equal(parseRav1(item.content).valid, item.kind === 'valid', item.filename)
}
assert.ok(parseRav1(fixture('rav1-frame.bad-magic.json')).errors.includes('magic'))
assert.ok(parseRav1(fixture('rav1-frame.length-mismatch.json')).errors.includes('length'))
assert.ok(parseRav1(fixture('rav1-frame.zero-sequence.json')).errors.includes('sequence'))
assert.ok(parseRav1('524156310000000100002001' + '00'.repeat(8193)).errors.includes('max'))
assert.equal(parseRav1('524156310000000100002001' + '00'.repeat(8193), 65536).valid, true)

// Envelope §4.1.
assert.equal(classifyEnvelope(200, fixture('envelope.success.json')).kind, 'success')
assert.equal(classifyEnvelope(200, fixture('envelope.business-error.json')).kind, 'business-error')
assert.equal(classifyEnvelope(401, { code: 401, data: null }).kind, 'token-prehandler')
assert.equal(
  classifyEnvelope(404, { code: 404, data: null }, '/api/v1/aiot/appearance/x').kind,
  'appearance-route-error'
)
assert.equal(classifyEnvelope(416, '', '/api/v1/aiot/appearance/x').kind, 'appearance-range')
assert.equal(
  classifyEnvelope(404, { code: 404, data: null }, '/api/v1/aiot/devices/x').kind,
  'unclassified'
)
assert.equal(classifyEnvelope(200, { code: 409, data: { bad: true } }).dataWarning, true)

// Binding §5.
assert.equal(
  transitionBinding(transitionBinding('unbound', 'request-ok'), 'confirmed'),
  'bound'
)
assert.equal(transitionBinding('pending', 'invalid-response'), 'pending')
assert.equal(transitionBinding('pending', 'unknown'), 'pending')
assert.equal(transitionBinding('pending', 'poll-404'), 'unbound')
assert.equal(transitionBinding('pending', 'awaiting-device'), 'awaiting')
assert.equal(transitionBinding('awaiting', 'trust-sealed'), 'pending')
assert.equal(transitionBinding('bound', 'token-403', 'legacy'), 'unbound')
assert.equal(transitionBinding('bound', 'token-404', 'pin'), 'bound')
assert.equal(transitionBinding('unbinding', 'unbind-failed'), 'unbinding')
assert.equal(transitionBinding('unbinding', 'unbind-confirmed'), 'unbound')
assert.equal(transitionBinding('bound', 'confirmed'), 'bound')
assert.deepEqual(allowedBindingEvents('awaiting'), ['trust-sealed'])

// Voice §5 gates.
const live = {
  generation: 7,
  sessionId: 'rv-7f3c2a9e1b4d4c6a',
  playbackEpoch: 1,
  lastSequence: 3,
  epochObserved: true
}
const msg = (extra) => ({
  generation: 7,
  sessionId: live.sessionId,
  type: 'output.start',
  playbackEpoch: 2,
  ...extra
})
assert.equal(gateMessage(live, msg({ generation: 6 })).gate, 'generation')
assert.equal(gateMessage(live, msg({ sessionId: 'old' })).gate, 'sessionId')
assert.equal(gateMessage(live, msg({ playbackEpoch: 1 })).gate, 'playbackEpoch')
assert.equal(gateMessage(live, msg({ playbackEpoch: undefined })).gate, 'playbackEpoch')
assert.equal(
  gateMessage(live, { generation: 7, sessionId: live.sessionId, type: 'rav1', sequence: 3 }).gate,
  'sequence'
)
assert.equal(gateMessage(live, msg({})).accepted, true)
assert.equal(
  gateMessage({ ...live, playbackEpoch: 0, epochObserved: false }, msg({ playbackEpoch: undefined }))
    .accepted,
  true
)

// Replay ledger: replay, conflict, ordinary eviction (runs again), tombstone (reply not retained).
let ledger = submitCommand(emptyLedger(), 'a', '{"command":"ping"}').state
assert.equal(submitCommand(ledger, 'a', '{"command":"ping"}').outcome, 'replay')
assert.equal(submitCommand(ledger, 'a', '{"command":"ping"} ').outcome, 'command_conflict')
for (let i = 0; i < REPLAY_WINDOW; i += 1) {
  ledger = submitCommand(ledger, `n${i}`, '{}').state
}
assert.equal(ledger.entries.length, REPLAY_WINDOW)
assert.equal(submitCommand(ledger, 'a', '{"command":"ping"}').outcome, 'executed_after_eviction')
const big = submitCommand(emptyLedger(), 'b', '{"command":"ping"}', {
  replyRetained: false
}).state
assert.equal(submitCommand(big, 'b', '{"command":"ping"}').outcome, 'command_result_unavailable')
assert.equal(submitCommand(big, 'b', '{}').outcome, 'command_conflict')

// OTA §12: the signing string verifies against the contract fixture and test key.
const manifest = fixture('ota-manifest.signed-v2.json')
const signing = buildSignatureString(
  manifest.version
    ? {
        taskNo: manifest.taskNo,
        version: manifest.version,
        fileSize: manifest.fileSize,
        checksumValue: manifest.checksumValue
      }
    : {}
)
const key = fs.readFileSync(
  path.join(here, '../../contracts/rodak-aiot/v1/examples/ota-signing-public-key.pem')
)
const sig = Buffer.from(manifest.signatureValue, 'hex')
assert.equal(crypto.verify('sha256', Buffer.from(signing), key, sig), true)
assert.equal(
  crypto.verify(
    'sha256',
    Buffer.from(signing.replace(`\n${manifest.fileSize}\n`, `\n${manifest.fileSize + 1}\n`)),
    key,
    sig
  ),
  false
)

console.log('logic tests passed')
