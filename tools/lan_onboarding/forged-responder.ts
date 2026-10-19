import { randomBytes, createHash, X509Certificate } from 'node:crypto'
import { readFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { networkInterfaces } from 'node:os'
import { resolve } from 'node:path'
import { URL } from 'node:url'
import https from 'node:https'
import type { IncomingMessage, ServerResponse } from 'node:http'

type Mode = 'broadcast' | 'wrongcode' | 'mitm'
type Json = Record<string, unknown>
type MdnsResponder = {
  respond: (packet: unknown, destination?: unknown) => void
  on: (event: string, listener: (...args: never[]) => void) => void
  destroy: () => void
}

function rodakRequire(): NodeRequire {
  const root = arg('--rodak-root', resolve(process.cwd(), '../rodak'))!
  return createRequire(resolve(root, 'package.json'))
}
const SERVICE = '_rodak._tcp.local'
const MAX_DURATION_MS = 60 * 60 * 1000

interface Config {
  mode: Mode
  listen: string
  port: number
  advertiseAddress: string
  realServerId: string
  guessCode: string
  cert: string
  key: string
  upstream?: string
  upstreamCa?: string
  duration: number
}
interface Pending {
  requestId: string
  requestToken: string
  deviceKey: string
  salt: Buffer
  B: Buffer
  expiresAt: number
  challenge?: { srpA: string; srpM1: string; serverBind: string }
}

function log(event: string, fields: Json = {}): void {
  process.stdout.write(`${JSON.stringify({ time: new Date().toISOString(), event, ...fields })}\n`)
}
function arg(name: string, fallback?: string): string | undefined {
  const index = process.argv.indexOf(name)
  return index < 0 ? fallback : (process.argv[index + 1] ?? fallback)
}
function required(name: string): string {
  const value = arg(name)
  if (!value) throw new Error(`missing ${name}`)
  return value
}
function numberArg(name: string, fallback: number): number {
  const value = arg(name)
  if (value === undefined) return fallback
  const number = Number(value)
  if (!Number.isFinite(number)) throw new Error(`invalid ${name}`)
  return number
}
function discoverAddress(): string {
  for (const entries of Object.values(networkInterfaces()))
    for (const address of entries ?? [])
      if (!address.internal && address.family === 'IPv4' && !address.address.startsWith('169.254.'))
        return address.address
  return '127.0.0.1'
}
export function parseConfig(): Config {
  const mode = required('--mode') as Mode
  if (!['broadcast', 'wrongcode', 'mitm'].includes(mode))
    throw new Error('mode must be broadcast, wrongcode or mitm')
  const realServerId = (arg('--real-server-id', '') ?? '').toLowerCase()
  if (mode === 'broadcast' && !/^[0-9a-f]{64}$/.test(realServerId))
    throw new Error('--real-server-id must be 64 lowercase hex characters')
  const guessCode = arg('--guess-code', '000000')!
  if (!/^\d{6}$/.test(guessCode)) throw new Error('--guess-code must be six digits')
  const upstream = arg('--upstream')
  if (mode === 'mitm' && !upstream) throw new Error('--upstream is required for mitm')
  return {
    mode,
    listen: arg('--listen', '0.0.0.0')!,
    port: numberArg('--port', 9443),
    advertiseAddress: arg('--advertise-address', discoverAddress())!,
    realServerId,
    guessCode,
    cert: arg('--cert', '.codex-temp/forged/forged-cert.pem')!,
    key: arg('--key', '.codex-temp/forged/forged-key.pem')!,
    upstream,
    upstreamCa: arg('--upstream-ca'),
    duration: Math.min(Math.max(numberArg('--duration', 300), 1), MAX_DURATION_MS / 1000)
  }
}
function body(request: IncomingMessage): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    let size = 0
    request.on('data', (part: Buffer) => {
      size += part.length
      if (size <= 64 * 1024) chunks.push(part)
    })
    request.on('end', () => resolve(Buffer.concat(chunks)))
    request.on('error', reject)
  })
}
function jsonResponse(response: ServerResponse, status: number, data: Json): void {
  const encoded = Buffer.from(
    JSON.stringify({
      code: status,
      data,
      message: status < 300 ? 'ok' : 'forged responder'
    })
  )
  response.writeHead(status, {
    'content-type': 'application/json',
    'content-length': encoded.length,
    connection: 'close'
  })
  response.end(encoded)
}
function idForCertificate(certPath: string): string {
  const certificate = new X509Certificate(readFileSync(certPath))
  const spki = certificate.publicKey.export({ type: 'spki', format: 'der' })
  return createHash('sha256').update(spki).digest('hex')
}
function makeMdns(config: Config, serverId: string): { close: () => void } {
  const multicastDns = rodakRequire()('multicast-dns') as (
    options: Record<string, unknown>
  ) => MdnsResponder
  const dns = multicastDns({
    type: 'udp4',
    interface: config.advertiseAddress,
    bind: '0.0.0.0',
    reuseAddr: true,
    loopback: false
  })
  dns.on('error', () => log('mdns_error', { service: SERVICE }))
  const tlsName = `rodak-${serverId.slice(0, 16)}.local`
  const instance = `${tlsName.slice(0, -6)}.${SERVICE}`
  const records = (ttl = 8) => [
    { name: SERVICE, type: 'PTR', ttl, data: instance },
    {
      name: instance,
      type: 'SRV',
      ttl,
      flush: true,
      data: { port: config.port, target: tlsName, priority: 0, weight: 0 }
    },
    {
      name: instance,
      type: 'TXT',
      ttl,
      flush: true,
      data: [
        Buffer.from('v=1'),
        Buffer.from(`id=${serverId}`),
        Buffer.from('name=Forged responder')
      ]
    },
    {
      name: tlsName,
      type: 'A',
      ttl,
      flush: true,
      data: config.advertiseAddress
    }
  ]
  const announce = () => {
    try {
      dns.respond({ answers: records() })
    } catch {}
  }
  dns.on(
    'query',
    (packet: { questions?: Array<{ name: string; type: string }> }, source: unknown) => {
      if (
        (packet.questions ?? []).some(
          (question) =>
            question.name.toLowerCase() === SERVICE &&
            (question.type === 'PTR' || question.type === 'ANY')
        )
      )
        try {
          dns.respond({ answers: records(), additionals: records().slice(1) }, source as never)
        } catch {}
    }
  )
  const timer = setInterval(announce, 4000)
  announce()
  log('mdns_started', { service: SERVICE, tlsName, advertisedId: serverId })
  return {
    close: () => {
      clearInterval(timer)
      try {
        dns.respond({ answers: records(0) })
      } catch {}
      dns.destroy()
    }
  }
}
function proxyRequest(
  config: Config,
  request: IncomingMessage,
  response: ServerResponse,
  path: string,
  payload: Buffer
): void {
  const upstream = new URL(config.upstream!)
  const headers = {
    ...request.headers,
    host: upstream.host,
    connection: 'close',
    'content-length': String(payload.length)
  }
  const req = https.request(
    {
      protocol: upstream.protocol,
      hostname: upstream.hostname,
      port: upstream.port || 443,
      path,
      method: request.method,
      headers,
      ca: config.upstreamCa ? readFileSync(config.upstreamCa) : undefined,
      rejectUnauthorized: Boolean(config.upstreamCa)
    },
    (upstreamResponse) => {
      response.writeHead(upstreamResponse.statusCode ?? 502, upstreamResponse.headers)
      upstreamResponse.pipe(response)
      log('proxy_response', { path, status: upstreamResponse.statusCode ?? 0 })
    }
  )
  req.setTimeout(10_000, () => req.destroy(new Error('upstream timeout')))
  req.on('error', (error) => {
    log('proxy_error', { path, error: String(error).slice(0, 120) })
    if (!response.headersSent) jsonResponse(response, 502, { error: 'upstream unavailable' })
    else response.destroy()
  })
  req.end(payload)
}
export async function run(config: Config): Promise<void> {
  const { runSrp6aClient } = rodakRequire()(
    './src/main/server/srp6a-client'
  ) as typeof import('../../../rodak/src/main/server/srp6a-client')
  const { computeTrustSetupBinding, deriveTrustSetupKeys } = rodakRequire()(
    './src/main/server/trust-setup-protocol'
  ) as typeof import('../../../rodak/src/main/server/trust-setup-protocol')
  const certId = idForCertificate(config.cert)
  const advertisedId = config.mode === 'broadcast' ? config.realServerId : certId
  const pending = new Map<string, Pending>()
  const tls = https.createServer(
    { cert: readFileSync(config.cert), key: readFileSync(config.key) },
    async (request, response) => {
      const path = request.url?.split('?')[0] ?? '/'
      log('request', { mode: config.mode, stage: path, spki: certId })
      if (config.mode === 'mitm') {
        proxyRequest(config, request, response, request.url ?? '/', await body(request))
        return
      }
      if (request.method === 'GET' && path === '/api/v1/app/health') {
        jsonResponse(response, 200, { ok: true })
        return
      }
      if (config.mode === 'broadcast') {
        jsonResponse(response, 404, { error: 'probe only' })
        return
      }
      if (request.method === 'POST' && path === '/api/v1/aiot/devices/binding/request') {
        let input: Json
        try {
          input = JSON.parse((await body(request)).toString('utf8')) as Json
        } catch {
          jsonResponse(response, 400, { error: 'invalid json' })
          return
        }
        const trust = input.trustSetup as Json | undefined
        const deviceKey = typeof input.deviceKey === 'string' ? input.deviceKey : ''
        if (
          !trust ||
          typeof trust.srpSalt !== 'string' ||
          typeof trust.srpB !== 'string' ||
          !deviceKey
        ) {
          jsonResponse(response, 400, { error: 'trust setup required' })
          return
        }
        const requestId = randomBytes(12).toString('hex')
        const requestToken = randomBytes(24).toString('base64url')
        const item: Pending = {
          requestId,
          requestToken,
          deviceKey,
          salt: Buffer.from(trust.srpSalt, 'base64'),
          B: Buffer.from(trust.srpB, 'base64'),
          expiresAt: Date.now() + 120000
        }
        pending.set(requestId, item)
        log('request_created', {
          stage: 'binding',
          requestId,
          deviceKey: deviceKey.slice(0, 8)
        })
        jsonResponse(response, 200, {
          requestId,
          trustSetup: { required: true },
          expiresAt: new Date(item.expiresAt).toISOString(),
          status: 'pending',
          pollIntervalSeconds: 1,
          requestToken
        })
        return
      }
      const statusMatch = path.match(
        /^\/api\/v1\/aiot\/devices\/binding\/requests\/([^/]+)\/status$/
      )
      if (request.method === 'GET' && statusMatch) {
        const item = pending.get(statusMatch[1])
        if (!item || request.headers.authorization !== `Bearer ${item.requestToken}`) {
          jsonResponse(response, 401, { error: 'unauthorized' })
          return
        }
        if (!item.challenge) {
          let srp: ReturnType<typeof runSrp6aClient>
          try {
            srp = runSrp6aClient({
              password: config.guessCode,
              salt: item.salt,
              B: item.B
            })
          } catch {
            log('challenge_error', {
              stage: 'wrongcode',
              requestId: item.requestId
            })
            jsonResponse(response, 400, { error: 'invalid trust setup' })
            return
          }
          const keys = deriveTrustSetupKeys(srp.K)
          const seenSpki = Buffer.from(certId, 'hex')
          const serverBind = computeTrustSetupBinding(keys.bind, {
            requestId: item.requestId,
            deviceKey: item.deviceKey,
            seenSpki,
            serverSpki: seenSpki,
            role: 'server'
          })
          item.challenge = {
            srpA: srp.A.toString('base64'),
            srpM1: srp.M1.toString('base64'),
            serverBind: serverBind.toString('base64')
          }
        }
        log('challenge_issued', {
          stage: 'wrongcode',
          requestId: item.requestId,
          guess: '******'
        })
        jsonResponse(response, 200, {
          status: 'awaiting-device',
          trustSetup: item.challenge
        })
        return
      }
      const trustMatch = path.match(/^\/api\/v1\/aiot\/devices\/binding\/requests\/([^/]+)\/trust$/)
      if (request.method === 'POST' && trustMatch) {
        const item = pending.get(trustMatch[1])
        if (!item || request.headers.authorization !== `Bearer ${item.requestToken}`) {
          jsonResponse(response, 401, { error: 'unauthorized' })
          return
        }
        const input = JSON.parse((await body(request)).toString('utf8')) as Json
        log('trust_result', {
          stage: 'wrongcode',
          requestId: item.requestId,
          result: typeof input.abort === 'string' ? input.abort : 'unexpected'
        })
        jsonResponse(response, 200, { status: 'revoked' })
        return
      }
      jsonResponse(response, 404, { error: 'not found' })
    }
  )
  tls.requestTimeout = 15_000
  await new Promise<void>((resolve, reject) => {
    tls.once('error', reject)
    tls.listen(config.port, config.listen, () => {
      tls.off('error', reject)
      resolve()
    })
  })
  const mdns = makeMdns(config, advertisedId)
  log('ready', {
    mode: config.mode,
    listen: config.listen,
    port: config.port,
    advertisedId,
    certificateSpki: certId,
    durationSeconds: config.duration
  })
  let resolveShutdown!: () => void
  const shutdown = new Promise<void>((resolve) => {
    resolveShutdown = resolve
  })
  let resolveDuration!: () => void
  const duration = new Promise<void>((resolve) => {
    resolveDuration = resolve
  })
  let stopped = false
  const stop = () => {
    if (stopped) return
    stopped = true
    mdns.close()
    tls.close()
    log('stopped', { mode: config.mode })
    resolveShutdown()
  }
  process.once('SIGINT', stop)
  process.once('SIGTERM', stop)
  const durationTimer = setTimeout(() => {
    resolveDuration()
    resolveShutdown()
  }, config.duration * 1000)
  durationTimer.unref()
  await Promise.race([shutdown, duration])
  clearTimeout(durationTimer)
  stop()
}
if (process.argv[1] && process.argv[1].endsWith('forged-responder.ts')) {
  void run(parseConfig()).catch((error) => {
    log('fatal', { error: String(error).slice(0, 200) })
    process.exitCode = 1
  })
}
