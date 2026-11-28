export type Rav1Error = 'hex' | 'short' | 'magic' | 'sequence' | 'length' | 'max'

export interface Rav1ParseResult {
  hex: string
  magic: string
  sequence: number
  length: number
  payloadHex: string
  remaining: number
  valid: boolean
  errors: Rav1Error[]
}

export type EnvelopeKind =
  | 'success'
  | 'business-error'
  | 'token-prehandler'
  | 'appearance-route-error'
  | 'appearance-range'
  | 'invalid-json'
  | 'unclassified'

export interface EnvelopeVerdict {
  kind: EnvelopeKind
  ok: boolean
  code?: unknown
  data?: unknown
  dataWarning: boolean
}

export type BindingState = 'unbound' | 'pending' | 'awaiting' | 'bound' | 'unbinding'
export type TrustMode = 'legacy' | 'pin'

export interface VoiceGateState {
  generation: number
  sessionId: string
  playbackEpoch: number
  lastSequence: number
  epochObserved: boolean
}

export interface VoiceGateMessage {
  type: 'output.start' | 'rav1' | string
  generation: number
  sessionId: string
  playbackEpoch?: number
  sequence?: number
}

export type VoiceGateName = 'generation' | 'sessionId' | 'playbackEpoch' | 'sequence'

export interface VoiceGateResult {
  accepted: boolean
  gate: VoiceGateName | null
}

export interface ReplayLedgerEntry {
  commandNo: string
  raw: string
  retained: boolean
}

export interface ReplayLedgerState {
  entries: ReplayLedgerEntry[]
  evicted: string[]
}

export type ReplayOutcome =
  | 'first_execution'
  | 'replay'
  | 'command_conflict'
  | 'command_result_unavailable'
  | 'executed_after_eviction'

export interface SubmitCommandResult {
  state: ReplayLedgerState
  outcome: ReplayOutcome
}

export interface OtaSignatureFields {
  taskNo: string | number
  version: string
  fileSize: string | number
  checksumValue: string
}

export const REPLAY_WINDOW: number
export const BINDING_TRANSITIONS: Record<
  BindingState,
  Record<string, BindingState | ((mode: TrustMode) => BindingState)>
>

export function parseRav1(hex: string, maxAudioFrameBytes?: number): Rav1ParseResult
export function classifyEnvelope(httpStatus: number, input: unknown, route?: string): EnvelopeVerdict
export function allowedBindingEvents(state: BindingState): string[]
export function transitionBinding(state: BindingState, event: string, mode?: TrustMode): BindingState
export function gateMessage(current: VoiceGateState, message: VoiceGateMessage): VoiceGateResult
export function emptyLedger(): ReplayLedgerState
export function submitCommand(
  state: ReplayLedgerState,
  commandNo: string,
  rawPayload: string,
  options?: { replyRetained?: boolean }
): SubmitCommandResult
export function buildSignatureString(fields: OtaSignatureFields): string
