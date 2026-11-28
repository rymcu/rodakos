<script setup lang="ts">
import {
  gateMessage,
  parseRav1,
  type Rav1Error,
  type VoiceGateMessage,
  type VoiceGateName
} from '../../../shared/protocol-logic.mjs'

definePageMeta({
  layout: 'protocols'
})

const { t, getFixture } = useProtocolFixtures()

useSeoMeta({
  title: () => `${t('实时语音与 RAV1 · 协议实验室', 'Realtime Voice & RAV1 · Protocol Lab')} · RodakOS`,
  description: () =>
    t(
      '单步演练实时语音对话与四道门控拦截、按字节解析 12+N RAV1 二进制音频帧，并浏览服务器主动播报 Announce 的 5 条分支。',
      'Step through realtime voice sessions and inject stale messages into the four gates, parse RAV1 binary frames byte by byte, and inspect all 5 announce branches.'
    )
})

const SID = 'rv-7f3c2a9e1b4d4c6a'
const STEPS: readonly ['→' | '←', string, string, string][] = [
  ['→', 'session.open {generation: 7}', '每个连接代次只发一次。', 'Sent once per connection generation.'],
  ['←', `session.ready {generation: 7, sessionId: "${SID}"}`, '会话已协商，但交互还没建立。', 'Session negotiated, but the interaction is not established yet.'],
  ['→', 'wake.detected {text}', '只在第一轮发送。', 'Sent on the first turn only.'],
  ['→', 'input.start {mode: "realtime"}', '第一个 input.start 成功：交互建立。此前任何失败都结束本次唤醒，不重连。', 'The first input.start succeeded: the interaction is established. Any failure before this ends the wake without reconnecting.'],
  ['←', 'output.start {playbackEpoch: 1}', '第一个显式 epoch。之后不带 epoch 的 output.start 会被拒绝。', 'The first explicit epoch. An epoch-less output.start is rejected from now on.'],
  ['←', 'RAV1 sequence=1', '下行音频开始。', 'Downlink audio starts.'],
  ['←', 'RAV1 sequence=2', '序号必须严格递增。', 'The sequence must strictly increase.'],
  ['←', 'output.stop {playbackEpoch: 1}', '这段回复的音频结束。设备先播完本地缓冲。', 'No more audio for this reply. The device drains local playback first.'],
  ['→', 'input.start', '下一轮：同一会话，不再发 wake.detected。', 'Next turn: same session, no new wake.detected.'],
  ['←', 'session.end {reason: "goodbye-intent"}', '任何 reason 都按普通结束处理，重新监听唤醒词。', 'Every reason is an ordinary end; listen for the wake word again.']
]

const stepIdx = shallowRef(0)

const gateState = computed(() => ({
  generation: 7,
  sessionId: stepIdx.value >= 2 ? SID : '',
  playbackEpoch: stepIdx.value >= 5 ? 1 : 0,
  lastSequence: stepIdx.value >= 7 ? 2 : stepIdx.value >= 6 ? 1 : 0,
  epochObserved: stepIdx.value >= 5,
  established: stepIdx.value >= 4
}))

const stepNote = computed(() => {
  if (stepIdx.value === 0) {
    return t('本地检测到唤醒词，建立 WebSocket。', 'Local wake word detected; open the WebSocket.')
  }
  const s = STEPS[stepIdx.value - 1]!
  return t(s[2], s[3])
})

const STALE_OPTIONS = computed(() => [
  { value: 'generation', label: t('旧连接（generation 6）的 output.start', 'output.start from old connection (generation 6)') },
  { value: 'sessionId', label: t('旧会话的 output.start', 'output.start from old session') },
  { value: 'playbackEpoch', label: t('epoch 没有递增的 output.start', 'output.start whose epoch did not increase') },
  { value: 'missingEpoch', label: t('不带 epoch 的 output.start', 'output.start without an epoch') },
  { value: 'sequence', label: t('序号没有递增的 RAV1 帧', 'RAV1 frame whose sequence did not increase') }
])

const selectedStale = shallowRef('generation')
const injectedJson = shallowRef('')
const gateVerdict = shallowRef<{ color: 'error' | 'warning'; text: string } | null>(null)

const GATE_TEXT: Record<VoiceGateName, [string, string]> = {
  generation: ['generation 门：消息来自旧的 WebSocket 连接。', 'generation gate: the message comes from an old WebSocket connection.'],
  sessionId: ['sessionId 门：消息属于另一个会话。', 'sessionId gate: the message belongs to another session.'],
  playbackEpoch: ['playbackEpoch 门：epoch 没有递增，或在已观察到 epoch 后缺少 epoch。', 'playbackEpoch gate: epoch did not increase or is missing after an explicit epoch was observed.'],
  sequence: ['sequence 门：RAV1 序号必须严格大于上一帧。', 'sequence gate: RAV1 sequence must strictly exceed the previous frame.']
}

function injectStale() {
  const s = gateState.value
  const msg: VoiceGateMessage = {
    type: 'output.start',
    generation: s.generation,
    sessionId: s.sessionId,
    playbackEpoch: s.playbackEpoch + 1
  }
  if (selectedStale.value === 'generation') msg.generation = 6
  if (selectedStale.value === 'sessionId') msg.sessionId = 'rv-0000000000000000'
  if (selectedStale.value === 'playbackEpoch') msg.playbackEpoch = s.playbackEpoch
  if (selectedStale.value === 'missingEpoch') delete msg.playbackEpoch
  if (selectedStale.value === 'sequence') {
    msg.type = 'rav1'
    delete msg.playbackEpoch
    msg.sequence = s.lastSequence
  }
  injectedJson.value = JSON.stringify(msg)
  const result = gateMessage(s, msg)
  if (result.accepted) {
    gateVerdict.value = {
      color: 'warning',
      text: t(
        '被接受：在当前步骤这条消息并不陈旧。例如还没观察到 epoch 时，不带 epoch 的 output.start 仍然兼容。往后推进几步再试。',
        'Accepted: at this step the message is not stale (e.g. before any epoch is observed, an epoch-less output.start is still accepted). Advance a few steps and retry.'
      )
    }
  } else if (result.gate) {
    const [zh, en] = GATE_TEXT[result.gate]
    gateVerdict.value = {
      color: 'error',
      text: `${t('被拒绝 · ', 'Rejected · ')}${t(zh, en)}`
    }
  }
}

function advanceStep() {
  stepIdx.value = Math.min(STEPS.length, stepIdx.value + 1)
  injectedJson.value = ''
  gateVerdict.value = null
}

function resetTimeline() {
  stepIdx.value = 0
  injectedJson.value = ''
  gateVerdict.value = null
}

// RAV1 Binary Frame Parser
const RAV1_FIXTURES = [
  { value: 'rav1-frame.opus.json', label: 'valid · rav1-frame.opus' },
  { value: 'rav1-frame.later.json', label: 'valid · rav1-frame.later' },
  { value: 'rav1-frame.bad-magic.json', label: 'invalid · bad-magic' },
  { value: 'rav1-frame.length-mismatch.json', label: 'invalid · length-mismatch' },
  { value: 'rav1-frame.zero-sequence.json', label: 'invalid · zero-sequence' }
]

const selectedRav1Fixture = shallowRef('rav1-frame.opus.json')
const rav1MaxBytes = shallowRef(8192)
const rav1Hex = shallowRef('52415631000000010000000778a1b2c3d4e5f6')

const RAV1_ERRORS: Record<Rav1Error, [string, string]> = {
  hex: ['不是合法的十六进制', 'not valid hex'],
  short: ['不足 12 字节帧头', 'shorter than 12-byte header'],
  magic: ['magic 不是 RAV1', 'magic is not RAV1'],
  sequence: ['序号为 0', 'sequence is 0'],
  length: ['长度字段不等于剩余字节数', 'length does not match remaining bytes'],
  max: ['超过 maxAudioFrameBytes', 'exceeds maxAudioFrameBytes']
}

const rav1Parsed = computed(() => parseRav1(rav1Hex.value, Number(rav1MaxBytes.value) || 8192))

const rav1ByteTokens = computed(() => {
  const pairs = rav1Parsed.value.hex.match(/.{1,2}/g) ?? []
  return pairs.map((b, i) => ({
    byte: b,
    kind: i < 4 ? 'magic' : i < 8 ? 'seq' : i < 12 ? 'len' : 'payload'
  }))
})

const rav1ErrorText = computed(() =>
  rav1Parsed.value.errors.map((e) => t(RAV1_ERRORS[e][0], RAV1_ERRORS[e][1])).join(t('；', '; '))
)

async function loadRav1Fixture() {
  const content = await getFixture(selectedRav1Fixture.value)
  if (content !== null) {
    rav1Hex.value = typeof content === 'string' ? content : JSON.stringify(content)
  }
}

watch(selectedRav1Fixture, () => {
  void loadRav1Fixture()
})

onMounted(() => {
  void loadRav1Fixture()
})

// Server-initiated Announce branches
const ANNOUNCE_BRANCHES = computed(() => [
  {
    id: 'played',
    label: t('正常播放', 'Played'),
    flow: [
      ['MQTT', 'voice.announce {announceSeq: 17, priority: "normal"}'],
      ['MQTT', 'ACK {"status":"ok"}'],
      ['WS', 'session.open {features: {announce: true, announceSeq: 17}}'],
      ['WS', 'session.ready'],
      ['WS', 'output.start → RAV1 … → output.stop'],
      ['WS', 'session.end {reason: "announce-finished"}']
    ],
    note: t(
      '设备不开麦克风，不发 wake.detected / input.start / vad，播完不进入下一轮。announceSeq 只出现在这一次 session.open 里。',
      'No microphone, no wake.detected / input.start / vad, no follow-up turn. announceSeq appears only in this session.open.'
    )
  },
  {
    id: 'busy',
    label: t('设备忙', 'Busy'),
    flow: [
      ['MQTT', 'voice.announce {announceSeq: 18, priority: "normal"}'],
      ['MQTT', 'ACK {"status":"error","errorCode":"busy"}']
    ],
    note: t(
      '语音助手不空闲，或 normal 播报时本地音乐在播，或命令队列已满。Rodak 记为普通失败命令，不重试。只有精确字符串 "urgent" 能暂停音乐后播报。',
      'Assistant is busy, local music plays during a normal announce, or command queue is full. Rodak records a normal failed command without retry. Only exact "urgent" pauses music.'
    )
  },
  {
    id: 'expired',
    label: t('认领过期', 'Expired'),
    flow: [
      ['MQTT', 'voice.announce {announceSeq: 19}'],
      ['MQTT', 'ACK {"status":"ok"}'],
      ['WS', t('（超过 30 秒才连上）', '(connects after more than 30 s)')],
      ['WS', 'session.open {features: {announce: true, announceSeq: 19}}'],
      ['WS', 'session.ready'],
      ['WS', 'session.end {reason: "announce-expired"}']
    ],
    note: t(
      'Rodak 只把播报文字登记 30 秒。迟到或未知的 announceSeq 立即返回 announce-expired，没有音频。ACK 后建连失败不会发第二个 ACK，由该窗口兜底。',
      'Rodak retains announce text for 30 s only. Late or unknown announceSeq returns announce-expired immediately with no audio.'
    )
  },
  {
    id: 'interrupt',
    label: t('本地打断', 'Local interrupt'),
    flow: [
      ['WS', 'output.start → RAV1 …'],
      ['WS', t('（用户说了唤醒词）', '(user says the wake word)')],
      ['WS', t('设备直接关闭 WebSocket', 'device closes the WebSocket')]
    ],
    note: t(
      '不发 playback.abort，不发 input.stop，也不把播报转为对话。',
      'No playback.abort, no input.stop, and the announcement does not become a conversation.'
    )
  },
  {
    id: 'failure',
    label: t('传输失败', 'Transport failure'),
    flow: [
      ['WS', 'output.start → RAV1 …'],
      ['WS', t('（网络断开）', '(network drops)')],
      ['WS', t('会话结束，不重连', 'session ends; no reconnect')]
    ],
    note: t(
      '重连会打开不带播报标记的普通会话，因此播报会话绝不重连。是否重新排队由 Rodak 决定，重试会分配新的 announceSeq。',
      'Reconnecting would open an ordinary session without the announce flag, so announce sessions never reconnect. Retries use a fresh announceSeq.'
    )
  }
])

const activeBranchIdx = shallowRef(0)
const activeBranch = computed(() => ANNOUNCE_BRANCHES.value[activeBranchIdx.value] ?? ANNOUNCE_BRANCHES.value[0]!)
</script>

<template>
  <div class="space-y-8">
    <UPageHeader
      headline="Realtime voice v1 · §2–§6 §8"
      :title="t('实时语音、四重门控与 RAV1 帧解析', 'Realtime Voice, Four Gates & RAV1 Parser')"
      :description="
        t(
          '空闲时不保持语音连接。本地唤醒或接受 voice.announce 后才按需建立 WebSocket，携带 AIoT access token 作 Bearer。',
          'No voice socket stays open while idle. The device connects on demand after a local wake or accepted voice.announce using the AIoT access token.'
        )
      "
    />

    <!-- 一次对话与四道门控 -->
    <UCard
      id="timeline-card"
      variant="subtle"
      :ui="{ root: 'ring-1 ring-default scroll-mt-28', body: 'space-y-5 p-5 sm:p-6' }"
    >
      <div class="space-y-1">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('一次对话与四道门控', 'One conversation and the four gates') }}
        </h2>
        <p class="text-xs leading-relaxed text-muted">
          {{
            t(
              '逐步推进会话，右侧同步更新设备门控状态；随时注入一条陈旧消息，观察它被哪一道门拦下。',
              'Advance the session step by step and inject a stale message at any time to see which gate rejects it.'
            )
          }}
        </p>
      </div>

      <div class="grid gap-6 lg:grid-cols-2">
        <div class="space-y-3">
          <div class="space-y-1.5">
            <div
              v-for="([dir, text], idx) in STEPS"
              :key="idx"
              class="flex items-start gap-2 rounded-lg border-l-3 px-3 py-2 font-mono text-xs transition-colors"
              :class="
                idx === stepIdx - 1
                  ? 'border-primary bg-primary/12 font-semibold text-highlighted'
                  : idx < stepIdx - 1
                    ? 'border-primary/60 bg-default/65 text-toned'
                    : 'border-default bg-default/30 text-muted'
              "
            >
              <span class="w-16 shrink-0 text-[11px] text-primary">
                {{ dir === '→' ? t('设备 →', 'device →') : t('← 服务器', '← server') }}
              </span>
              <span class="break-all">{{ text }}</span>
            </div>
          </div>

          <div class="flex flex-wrap gap-2">
            <UButton
              :label="t('下一步', 'Next step')"
              icon="i-lucide-step-forward"
              color="primary"
              size="sm"
              :disabled="stepIdx >= STEPS.length"
              @click="advanceStep"
            />
            <UButton
              :label="t('重置', 'Reset')"
              icon="i-lucide-rotate-ccw"
              color="neutral"
              variant="outline"
              size="sm"
              @click="resetTimeline"
            />
          </div>
        </div>

        <div class="space-y-4">
          <div class="space-y-2">
            <h3 class="text-sm font-semibold text-highlighted">
              {{ t('设备当前门控状态', 'Current device gate state') }}
            </h3>
            <div class="overflow-hidden rounded-lg border border-default">
              <table class="w-full border-collapse text-left text-xs">
                <tbody>
                  <tr class="border-b border-default/60">
                    <th class="w-36 bg-elevated/60 px-3 py-2 font-medium text-muted">generation</th>
                    <td class="px-3 py-2 font-mono text-highlighted">{{ gateState.generation }}</td>
                  </tr>
                  <tr class="border-b border-default/60">
                    <th class="bg-elevated/60 px-3 py-2 font-medium text-muted">sessionId</th>
                    <td class="px-3 py-2 font-mono text-highlighted">{{ gateState.sessionId || '—' }}</td>
                  </tr>
                  <tr class="border-b border-default/60">
                    <th class="bg-elevated/60 px-3 py-2 font-medium text-muted">playbackEpoch</th>
                    <td class="px-3 py-2 font-mono text-highlighted">
                      {{ gateState.epochObserved ? gateState.playbackEpoch : '—' }}
                    </td>
                  </tr>
                  <tr class="border-b border-default/60">
                    <th class="bg-elevated/60 px-3 py-2 font-medium text-muted">lastSequence</th>
                    <td class="px-3 py-2 font-mono text-highlighted">{{ gateState.lastSequence || '—' }}</td>
                  </tr>
                  <tr>
                    <th class="bg-elevated/60 px-3 py-2 font-medium text-muted">
                      {{ t('交互已建立', 'established') }}
                    </th>
                    <td class="px-3 py-2">
                      <UBadge
                        :label="gateState.established ? t('是', 'yes') : t('否', 'no')"
                        :color="gateState.established ? 'success' : 'neutral'"
                        variant="subtle"
                        size="xs"
                      />
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>

          <UAlert icon="i-lucide-info" color="primary" variant="subtle" :description="stepNote" />

          <div class="space-y-2.5 rounded-lg border border-default bg-default/70 p-4">
            <h3 class="text-sm font-semibold text-highlighted">
              {{ t('注入陈旧消息', 'Inject a stale message') }}
            </h3>
            <div class="flex flex-col gap-2 sm:flex-row">
              <USelect
                v-model="selectedStale"
                :items="STALE_OPTIONS"
                value-key="value"
                label-key="label"
                class="flex-1"
              />
              <UButton
                :label="t('注入', 'Inject')"
                icon="i-lucide-zap"
                color="primary"
                variant="soft"
                @click="injectStale"
              />
            </div>

            <pre
              v-if="injectedJson"
              class="overflow-x-auto rounded-md border border-default bg-elevated/50 p-2.5 font-mono text-xs text-toned"
            >{{ injectedJson }}</pre>

            <UAlert
              v-if="gateVerdict"
              :color="gateVerdict.color"
              variant="subtle"
              :icon="gateVerdict.color === 'error' ? 'i-lucide-shield-ban' : 'i-lucide-alert-triangle'"
              :description="gateVerdict.text"
            />
          </div>
        </div>
      </div>
    </UCard>

    <!-- RAV1 帧解析器 -->
    <UCard
      id="rav1"
      variant="subtle"
      :ui="{ root: 'ring-1 ring-default scroll-mt-28', body: 'space-y-5 p-5 sm:p-6' }"
    >
      <div class="space-y-1">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('RAV1 帧解析器', 'RAV1 frame parser') }}
        </h2>
        <p class="text-xs leading-relaxed text-muted">
          {{
            t(
              '每条二进制消息 = 4 字节 RAV1 + 4 字节大端序号（非 0）+ 4 字节大端长度 + 单个 Opus 包；长度必须严格等于剩余字节数且不超过 maxAudioFrameBytes（默认 8192，规范上限 64 KiB）。',
              'Each binary frame = 4-byte RAV1 + 4-byte big-endian sequence (non-zero) + 4-byte big-endian length + one Opus packet, strictly matching remaining bytes and maxAudioFrameBytes.'
            )
          }}
        </p>
      </div>

      <div class="grid gap-4 sm:grid-cols-2">
        <div class="space-y-1.5">
          <label class="block text-xs font-medium text-highlighted">Fixture</label>
          <USelect
            v-model="selectedRav1Fixture"
            :items="RAV1_FIXTURES"
            value-key="value"
            label-key="label"
            class="w-full font-mono"
          />
        </div>
        <div class="space-y-1.5">
          <label class="block text-xs font-medium text-highlighted" for="rav1-max">maxAudioFrameBytes</label>
          <UInput id="rav1-max" v-model.number="rav1MaxBytes" type="number" :min="1" :max="65536" class="w-full font-mono" />
        </div>
      </div>

      <div class="space-y-1.5">
        <label class="block text-xs font-medium text-highlighted" for="rav1-hex">Hex</label>
        <UTextarea id="rav1-hex" v-model="rav1Hex" :rows="2" class="w-full font-mono" />
      </div>

      <!-- 彩色字节分布 -->
      <div class="space-y-2">
        <div class="flex flex-wrap gap-1.5 font-mono text-xs">
          <span
            v-for="(tok, idx) in rav1ByteTokens"
            :key="idx"
            class="rounded border px-1.5 py-0.5"
            :class="
              tok.kind === 'magic'
                ? 'border-sky-500/40 bg-sky-500/15 text-sky-400'
                : tok.kind === 'seq'
                  ? 'border-emerald-500/40 bg-emerald-500/15 text-emerald-400'
                  : tok.kind === 'len'
                    ? 'border-amber-500/40 bg-amber-500/15 text-amber-400'
                    : 'border-default bg-default/70 text-toned'
            "
          >
            {{ tok.byte }}
          </span>
        </div>

        <div class="flex flex-wrap gap-3 text-[11px] text-muted">
          <span class="inline-flex items-center gap-1.5">
            <span class="inline-block size-2.5 rounded-xs bg-sky-500/60" /> magic (0..3)
          </span>
          <span class="inline-flex items-center gap-1.5">
            <span class="inline-block size-2.5 rounded-xs bg-emerald-500/60" /> sequence (4..7)
          </span>
          <span class="inline-flex items-center gap-1.5">
            <span class="inline-block size-2.5 rounded-xs bg-amber-500/60" /> length (8..11)
          </span>
          <span class="inline-flex items-center gap-1.5">
            <span class="inline-block size-2.5 rounded-xs bg-slate-500/50" /> Opus payload (12+)
          </span>
        </div>
      </div>

      <div class="grid gap-3 sm:grid-cols-4">
        <div class="rounded-lg border border-default bg-default/70 p-3">
          <div class="text-[11px] text-muted">magic</div>
          <div class="mt-0.5 font-mono text-sm font-semibold text-highlighted">{{ rav1Parsed.magic || '—' }}</div>
        </div>
        <div class="rounded-lg border border-default bg-default/70 p-3">
          <div class="text-[11px] text-muted">sequence</div>
          <div class="mt-0.5 font-mono text-sm font-semibold text-highlighted">
            {{ Number.isFinite(rav1Parsed.sequence) ? rav1Parsed.sequence : '—' }}
          </div>
        </div>
        <div class="rounded-lg border border-default bg-default/70 p-3">
          <div class="text-[11px] text-muted">length</div>
          <div class="mt-0.5 font-mono text-sm font-semibold text-highlighted">
            {{ Number.isFinite(rav1Parsed.length) ? rav1Parsed.length : '—' }}
          </div>
        </div>
        <div class="rounded-lg border border-default bg-default/70 p-3">
          <div class="text-[11px] text-muted">{{ t('剩余字节', 'remaining bytes') }}</div>
          <div class="mt-0.5 font-mono text-sm font-semibold text-highlighted">{{ rav1Parsed.remaining }}</div>
        </div>
      </div>

      <UAlert
        :color="rav1Parsed.valid ? 'success' : 'error'"
        variant="subtle"
        :icon="rav1Parsed.valid ? 'i-lucide-check-circle-2' : 'i-lucide-x-circle'"
        :description="
          rav1Parsed.valid
            ? t('合法的 RAV1 帧。', 'Valid RAV1 frame.')
            : `${t('不合法：', 'Invalid: ')}${rav1ErrorText}`
        "
      />
    </UCard>

    <!-- 服务器主动播报 Announce -->
    <UCard
      id="announce"
      variant="subtle"
      :ui="{ root: 'ring-1 ring-default scroll-mt-28', body: 'space-y-5 p-5 sm:p-6' }"
    >
      <div class="space-y-1">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('服务器主动播报（Announce）', 'Server-initiated announcement') }}
        </h2>
        <p class="text-xs leading-relaxed text-muted">
          {{
            t(
              'MQTT voice.announce 只带 announceSeq 与 priority，不带播报文字。文字只在已认证的语音会话里下发；播报会话纯下行、不开麦克风、从不重连。',
              'MQTT voice.announce carries only announceSeq and priority — never text. Announce sessions are playback-only, open no mic, and never reconnect.'
            )
          }}
        </p>
      </div>

      <div class="flex flex-wrap gap-2">
        <UButton
          v-for="(br, idx) in ANNOUNCE_BRANCHES"
          :key="br.id"
          :label="br.label"
          :color="activeBranchIdx === idx ? 'primary' : 'neutral'"
          :variant="activeBranchIdx === idx ? 'solid' : 'outline'"
          size="xs"
          class="rounded-full"
          @click="activeBranchIdx = idx"
        />
      </div>

      <div class="space-y-1.5">
        <div
          v-for="([ch, text], idx) in activeBranch.flow"
          :key="idx"
          class="flex items-start gap-2 rounded-lg border-l-3 border-primary bg-default/70 px-3 py-2 font-mono text-xs text-toned"
        >
          <UBadge :label="ch" :color="ch === 'MQTT' ? 'neutral' : 'primary'" variant="subtle" size="xs" />
          <span class="break-all">{{ text }}</span>
        </div>
      </div>

      <UAlert icon="i-lucide-info" color="primary" variant="subtle" :description="activeBranch.note" />
    </UCard>
  </div>
</template>
