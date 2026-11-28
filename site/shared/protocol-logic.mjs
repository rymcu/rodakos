// Shared Rodak AIoT v1 & Realtime Voice v1 protocol verification logic.
// Used directly by both the Nuxt Protocol Lab pages and `node tests/protocol-logic.test.mjs`.

export function parseRav1(hex, maxAudioFrameBytes = 8192) {
  const clean = String(hex).replace(/\s+/g, '').toLowerCase()
  const errors = []
  if (!/^[0-9a-f]*$/.test(clean) || clean.length % 2) errors.push('hex')
  const bytes = Math.floor(clean.length / 2)
  if (bytes < 12) errors.push('short')
  const magic = clean.slice(0, 8)
  const sequence = bytes >= 8 ? Number.parseInt(clean.slice(8, 16), 16) : Number.NaN
  const length = bytes >= 12 ? Number.parseInt(clean.slice(16, 24), 16) : Number.NaN
  const remaining = Math.max(0, bytes - 12)
  if (magic !== '52415631') errors.push('magic')
  if (!Number.isFinite(sequence) || sequence === 0) errors.push('sequence')
  if (!Number.isFinite(length) || length !== remaining) errors.push('length')
  if (Number.isFinite(length) && (length > 65536 || length > maxAudioFrameBytes)) errors.push('max')
  return {
    hex: clean,
    magic: magic === '52415631' ? 'RAV1' : magic,
    sequence,
    length,
    payloadHex: clean.slice(24),
    remaining,
    valid: errors.length === 0,
    errors
  }
}

// Contract §4.1. `route` is the request path; the envelope itself does not carry it.
export function classifyEnvelope(httpStatus, input, route = '') {
  const appearanceRoute = String(route).includes('/appearance/')
  if (httpStatus === 416 && appearanceRoute && (input === '' || input == null)) {
    return { kind: 'appearance-range', ok: false, dataWarning: false }
  }
  let body
  try {
    body = typeof input === 'string' ? JSON.parse(input) : input
  } catch {
    return { kind: 'invalid-json', ok: false, dataWarning: false }
  }
  const isObject = body && typeof body === 'object'
  const code = isObject ? body.code : undefined
  const data = isObject ? body.data : undefined
  const result = { code, data, dataWarning: false }
  if (httpStatus === 416 && appearanceRoute) {
    return { ...result, kind: 'appearance-range', ok: false }
  }
  if (httpStatus === 401 && code === 401) {
    return { ...result, kind: 'token-prehandler', ok: false }
  }
  if (httpStatus === 200 && code === 200) {
    return { ...result, kind: 'success', ok: true }
  }
  if (appearanceRoute && httpStatus === code && code >= 400 && code <= 503) {
    return { ...result, kind: 'appearance-route-error', ok: false, dataWarning: data !== null }
  }
  if (httpStatus === 200 && Number.isInteger(code) && code >= 400) {
    return { ...result, kind: 'business-error', ok: false, dataWarning: data !== null }
  }
  return { ...result, kind: 'unclassified', ok: false }
}

// Contract §5. Events not listed for a state leave it unchanged.
export const BINDING_TRANSITIONS = {
  unbound: { 'request-ok': 'pending' },
  pending: {
    pending: 'pending',
    confirmed: 'bound',
    expired: 'unbound',
    revoked: 'unbound',
    'poll-401': 'unbound',
    'poll-404': 'unbound',
    'awaiting-device': 'awaiting',
    unknown: 'pending',
    'invalid-response': 'pending'
  },
  awaiting: { 'trust-sealed': 'pending' },
  bound: {
    'token-ok': 'bound',
    'token-403': (mode) => (mode === 'legacy' ? 'unbound' : 'bound'),
    'token-404': (mode) => (mode === 'legacy' ? 'unbound' : 'bound'),
    'unbind-request': 'unbinding'
  },
  unbinding: { 'unbind-failed': 'unbinding', 'unbind-confirmed': 'unbound' }
}

export function allowedBindingEvents(state) {
  return Object.keys(BINDING_TRANSITIONS[state] || {})
}

export function transitionBinding(state, event, mode = 'legacy') {
  const table = BINDING_TRANSITIONS[state] || BINDING_TRANSITIONS.unbound
  const target = table[event]
  if (target === undefined) return state in BINDING_TRANSITIONS ? state : 'unbound'
  return typeof target === 'function' ? target(mode) : target
}

// Realtime voice §5: four independent stale-result gates.
export function gateMessage(current, message) {
  if (message.generation !== current.generation) {
    return { accepted: false, gate: 'generation' }
  }
  if (message.sessionId !== current.sessionId) {
    return { accepted: false, gate: 'sessionId' }
  }
  if (message.type === 'output.start') {
    if (message.playbackEpoch !== undefined && message.playbackEpoch <= current.playbackEpoch) {
      return { accepted: false, gate: 'playbackEpoch' }
    }
    if (current.epochObserved && message.playbackEpoch === undefined) {
      return { accepted: false, gate: 'playbackEpoch' }
    }
  }
  if (message.type === 'rav1' && !(message.sequence > current.lastSequence)) {
    return { accepted: false, gate: 'sequence' }
  }
  return { accepted: true, gate: null }
}

// MQTT command semantics: 64-entry replay window + tombstones for replies exceeding 64 KiB budget.
export const REPLAY_WINDOW = 64

export function emptyLedger() {
  return { entries: [], evicted: [] }
}

export function submitCommand(state, commandNo, rawPayload, { replyRetained = true } = {}) {
  const id = String(commandNo)
  const raw = String(rawPayload)
  const entries = state.entries || []
  const evicted = state.evicted || []
  const found = entries.find((entry) => entry.commandNo === id)
  if (found) {
    if (found.raw !== raw) return { state, outcome: 'command_conflict' }
    return { state, outcome: found.retained ? 'replay' : 'command_result_unavailable' }
  }
  const wasEvicted = evicted.includes(id)
  const nextEntries = [...entries, { commandNo: id, raw, retained: replyRetained }]
  const nextEvicted = evicted.filter((x) => x !== id)
  while (nextEntries.length > REPLAY_WINDOW) nextEvicted.push(nextEntries.shift().commandNo)
  return {
    state: { entries: nextEntries, evicted: nextEvicted.slice(-256) },
    outcome: wasEvicted ? 'executed_after_eviction' : 'first_execution'
  }
}

// Contract §12. Only these four fields are covered by the RSA-2048 signature.
export function buildSignatureString({ taskNo, version, fileSize, checksumValue }) {
  return `rodakos-ota-v2\nrymcu-bigsmart\nota_0\nrsa2048-sha256\n${taskNo}\n${version}\n${fileSize}\n${checksumValue}\n`
}
