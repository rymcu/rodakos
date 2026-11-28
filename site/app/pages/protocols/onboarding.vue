<script setup lang="ts">
import {
  allowedBindingEvents,
  classifyEnvelope,
  transitionBinding,
  type BindingState,
  type TrustMode
} from '../../../shared/protocol-logic.mjs'

definePageMeta({
  layout: 'protocols'
})

const { t, getFixture } = useProtocolFixtures()

useSeoMeta({
  title: () => `${t('接入流程 · 协议实验室', 'Onboarding · Protocol Lab')} · RodakOS`,
  description: () =>
    t(
      '交互验证 AIoT v1 绑定状态机（legacy vs pin 模式）、§4.1 统一 HTTP 响应信封检查器与 expiresAt 同名字段陷阱。',
      'Interactive AIoT v1 binding state machine (legacy vs pin mode), §4.1 envelope inspector, and the expiresAt field-overwrite comparison.'
    )
})

const STATES: BindingState[] = ['unbound', 'pending', 'awaiting', 'bound', 'unbinding']

const EVENTS: readonly [string, string, string][] = [
  ['request-ok', 'POST binding/request 成功', 'POST binding/request OK'],
  ['pending', 'status = pending', 'status = pending'],
  ['confirmed', 'status = confirmed', 'status = confirmed'],
  ['expired', 'status = expired', 'status = expired'],
  ['revoked', 'status = revoked', 'status = revoked'],
  ['poll-401', '轮询得到 401', 'poll returns 401'],
  ['poll-404', '轮询得到 404', 'poll returns 404'],
  ['awaiting-device', 'status = awaiting-device', 'status = awaiting-device'],
  ['unknown', '未知 status 值', 'unknown status value'],
  ['invalid-response', '响应缺少必需字段或凭据', 'response missing required fields or credentials'],
  ['trust-sealed', 'POST /trust 封装回复', 'POST /trust sealed reply'],
  ['token-ok', 'auth/token 成功', 'auth/token OK'],
  ['token-403', 'auth/token 403', 'auth/token 403'],
  ['token-404', 'auth/token 404', 'auth/token 404'],
  ['unbind-request', '用户请求解绑', 'user requests unbind'],
  ['unbind-failed', '请求或清理失败', 'request or cleanup failed'],
  ['unbind-confirmed', 'data.status = unbound + 本地清理完成', 'data.status = unbound + cleanup done']
]

const INFO: Record<BindingState, [string, string]> = {
  unbound: ['没有有效的云凭据。MQTT 和语音关闭。', 'No active cloud credentials. MQTT and voice are off.'],
  pending: [
    '屏幕显示配对码，按 pollIntervalSeconds 轮询。MQTT 和语音必须保持禁用。',
    'The pairing code is on screen; poll every pollIntervalSeconds. MQTT and voice must stay disabled.'
  ],
  awaiting: [
    'LAN 信任建立：设备向 /trust 提交 SRP 证明。只有 LanOnboardingService 处理；主配对路径对 awaiting-device 安全失败。',
    'LAN trust setup: the device posts its SRP proof to /trust. Only LanOnboardingService handles it; the main pairing path fails closed on awaiting-device.'
  ],
  bound: [
    '令牌、MQTT、语音已作为一个快照提交。设备连接 MQTT，按需打开语音。',
    'Token, MQTT and voice are committed as one snapshot. The device connects MQTT and opens voice on demand.'
  ],
  unbinding: [
    '等待服务器确认和本地清理。任一步失败都保持 unbinding 并重试；完成前不能显示“已解绑”。',
    'Waiting for server confirmation and local cleanup. Any failure stays in unbinding and retries; do not show "unbound" before both succeed.'
  ]
}

const NOTES: Record<string, [string, string]> = {
  'poll-404': [
    'expired 只返回一次，服务器也会清理过期记录，所以设备可能直接得到 404。',
    'expired is returned once and the server also purges expired records, so the device may get 404 directly.'
  ],
  unknown: [
    '未知值：安全失败，不启用 Device Cloud。',
    'Unknown value: fail closed; do not enable Device Cloud.'
  ],
  'invalid-response': [
    '拒绝这个响应，不改动当前配置，继续轮询。',
    'Reject the response, keep the live configuration, keep polling.'
  ],
  'token-403-pin': [
    'pin 模式：保留身份，报告 credentials rejected，不自动重新配对。',
    'Pin mode: keep the identity, report credentials rejected, do not re-pair automatically.'
  ],
  'token-403-legacy': [
    'legacy 模式：清除令牌和 MQTT 配置，回到配对。',
    'Legacy mode: clear the token and MQTT configuration and return to pairing.'
  ],
  expired: [
    '清除保存的申请。重新配对需要用户明确操作。',
    'Clear the saved request. A new pairing needs an explicit user action.'
  ],
  revoked: [
    'revoked 的来源：主人取消、配对码错 5 次、信任建立失败、解绑。',
    'Causes of revoked: owner cancelled, 5 wrong codes, trust-setup failure, unbind.'
  ]
}

const TRANSITION_ROWS: readonly [string, string, string, string, string][] = [
  ['unbound', 'POST binding/request OK', 'pending', '先持久化 device secret，再发请求；先保存 requestId/token/code/expiresAt，再显示配对码。', 'Persist the device secret before sending; save requestId/token/code/expiresAt before showing the code.'],
  ['pending', 'pending', 'pending', 'MQTT 和语音必须保持禁用。', 'MQTT and voice must stay disabled.'],
  ['pending', 'confirmed', 'bound', '把令牌、MQTT、语音作为一个快照提交（§6.1）。', 'Commit token, MQTT and voice as one snapshot (§6.1).'],
  ['pending', 'expired / revoked', 'unbound', '清除保存的申请。重新配对需要用户明确操作。', 'Clear the saved request. A new pairing needs an explicit user action.'],
  ['pending', '轮询 401 / 404', 'unbound', '清除保存的申请。', 'Clear the saved request.'],
  ['pending', 'awaiting-device', 'awaiting', '只有 LanOnboardingService 处理；主配对路径安全失败。', 'Only LanOnboardingService handles it; the main pairing path fails closed.'],
  ['awaiting', 'POST /trust 封装回复', 'pending', '安装服务器信任，换用新的 request token 继续轮询。', 'Install the server trust and keep polling with the new request token.'],
  ['pending', '未知值', 'pending', '安全失败，不启用 Device Cloud。', 'Fail closed; do not enable Device Cloud.'],
  ['pending', '缺少必需字段或凭据', 'pending', '拒绝，不改动当前配置。', 'Reject; do not change the live configuration.'],
  ['bound', 'auth/token OK', 'bound', '更新令牌。', 'Update the token.'],
  ['bound', 'auth/token 403 / 404', 'unbound / bound', 'legacy：清除令牌和 MQTT，回到配对。pin：保留身份，报告 credentials rejected。', 'Legacy: clear token and MQTT, return to pairing. Pin: keep identity, report credentials rejected.'],
  ['bound', '用户请求解绑', 'unbinding', '带 access token 调用 POST binding/unbind。', 'Call POST binding/unbind with the access token.'],
  ['unbinding', '请求或清理失败', 'unbinding', '保持 unbinding 并重试。旧令牌重试可能得到 401：上一次可能已成功。', 'Stay unbinding and retry. A retry with the old token may get 401: the earlier call may have succeeded.'],
  ['unbinding', 'data.status = unbound + 清理', 'unbound', '清除令牌、申请、MQTT、语音；保留 WiFi 和配网 URL。', 'Clear token, request, MQTT and voice; keep WiFi and the provisioning URL.']
]

const bindingState = shallowRef<BindingState>('unbound')
const trustMode = shallowRef<TrustMode>('legacy')
const lastEvent = shallowRef<string | null>(null)

const allowedEvents = computed(() => new Set(allowedBindingEvents(bindingState.value)))

const stateNoteText = computed(() => {
  const base = INFO[bindingState.value]
  let extra: [string, string] | undefined
  if (lastEvent.value === 'token-403' || lastEvent.value === 'token-404') {
    extra = NOTES[`token-403-${trustMode.value}`]
  } else if (lastEvent.value && NOTES[lastEvent.value]) {
    extra = NOTES[lastEvent.value]
  }
  return t(base[0], base[1]) + (extra ? ` ${t(extra[0], extra[1])}` : '')
})

function triggerEvent(id: string) {
  lastEvent.value = id
  bindingState.value = transitionBinding(bindingState.value, id, trustMode.value)
}

function resetBinding() {
  bindingState.value = 'unbound'
  lastEvent.value = null
}

function setTrustMode(mode: TrustMode) {
  trustMode.value = mode
  resetBinding()
}

// Envelope Inspector
const ENVELOPE_PRESETS = [
  {
    zh: '成功',
    en: 'Success',
    status: 200,
    path: '/api/v1/aiot/devices/binding/requests/3f2b8c1e9a4d4f6b8e2a1c5d7b9e0f12/status',
    fixtureName: 'envelope.success.json'
  },
  {
    zh: '业务错误（HTTP 200）',
    en: 'Business error (HTTP 200)',
    status: 200,
    path: '/api/v1/aiot/devices/binding/requests/3f2b8c1e9a4d4f6b8e2a1c5d7b9e0f12/status',
    fixtureName: 'envelope.business-error.json'
  },
  {
    zh: '令牌预处理拒绝',
    en: 'Token pre-handler',
    status: 401,
    path: '/api/v1/ota/check',
    body: '{"code":401,"message":"Unauthorized","data":null}'
  },
  {
    zh: 'Appearance 路由错误',
    en: 'Appearance error',
    status: 404,
    path: '/api/v1/aiot/appearance/themes/x',
    body: '{"code":404,"message":"Not found","data":null}'
  },
  {
    zh: 'Appearance 无效 Range',
    en: 'Appearance bad Range',
    status: 416,
    path: '/api/v1/aiot/appearance/artifacts/x',
    body: ''
  },
  {
    zh: '错误却带 data',
    en: 'Error with data',
    status: 200,
    path: '/api/v1/aiot/devices/auth/token',
    body: '{"code":403,"message":"forbidden","data":{"x":1}}'
  }
] as const

const httpStatus = shallowRef(200)
const requestRoute = shallowRef(
  '/api/v1/aiot/devices/binding/requests/3f2b8c1e9a4d4f6b8e2a1c5d7b9e0f12/status'
)
const responseBody = shallowRef('{"code":200,"message":"ok","data":{}}')

const ENVELOPE_LABELS: Record<string, [string, string]> = {
  success: ['成功：HTTP 200 且 code 200，读取 data。', 'Success: HTTP 200 and code 200; read data.'],
  'business-error': [
    '业务错误：HTTP 是 200，但信封 code 表示失败。',
    'Business error: HTTP is 200, but the envelope code is a failure.'
  ],
  'token-prehandler': [
    'access token 预处理拒绝（缺失、无效或已撤销）：真正的 HTTP 401。',
    'Access-token pre-handler rejected (missing, invalid or revoked): a real HTTP 401.'
  ],
  'appearance-route-error': [
    'Appearance 路由错误：HTTP 状态等于业务码。',
    'Appearance route error: the HTTP status equals the business code.'
  ],
  'appearance-range': [
    'Appearance 无效 Range：HTTP 416，body 为空。',
    'Appearance invalid Range: HTTP 416 with an empty body.'
  ],
  'invalid-json': ['body 不是合法 JSON。', 'The body is not valid JSON.'],
  unclassified: ['不符合契约 §4.1 的任何一种情况。', 'Matches no case in contract §4.1.']
}

const envelopeResult = computed(() =>
  classifyEnvelope(Number(httpStatus.value), responseBody.value.trim(), requestRoute.value)
)

const envelopeSummary = computed(() => {
  const res = envelopeResult.value
  const label = ENVELOPE_LABELS[res.kind] ?? ENVELOPE_LABELS.unclassified!
  let text = t(label[0], label[1])
  if (res.code !== undefined) text += ` · code=${String(res.code)}`
  if (res.dataWarning) {
    text += ` · ${t('警告：错误响应的 data 应为 null。', 'Warning: data must be null in an error response.')}`
  }
  return text
})

const envelopeAlertColor = computed(() => {
  if (envelopeResult.value.ok) return 'success'
  if (envelopeResult.value.dataWarning) return 'warning'
  return 'error'
})

async function applyPreset(preset: (typeof ENVELOPE_PRESETS)[number]) {
  httpStatus.value = preset.status
  requestRoute.value = preset.path
  if ('fixtureName' in preset && preset.fixtureName) {
    const content = await getFixture(preset.fixtureName)
    if (content !== null) {
      responseBody.value = JSON.stringify(content, null, 2)
    }
  } else if ('body' in preset) {
    responseBody.value = preset.body
  }
}

const expPendingJson = shallowRef('')
const expConfirmedJson = shallowRef('')

onMounted(async () => {
  const envOk = await getFixture('envelope.success.json')
  if (envOk !== null) {
    responseBody.value = JSON.stringify(envOk, null, 2)
  }

  const pick = (obj: Record<string, unknown>, keys: string[]) =>
    Object.fromEntries(keys.filter((k) => k in obj).map((k) => [k, obj[k]]))

  const pending = await getFixture<Record<string, unknown>>('binding-status.pending.json')
  if (pending) {
    expPendingJson.value = JSON.stringify(pick(pending, ['status', 'expiresAt']), null, 2)
  }
  const confirmed = await getFixture<Record<string, unknown>>('binding-status.confirmed.json')
  if (confirmed) {
    expConfirmedJson.value = JSON.stringify(
      pick(confirmed, ['status', 'expiresAt', 'expires_at', 'expiresIn', 'accessToken']),
      null,
      2
    ).replace(/"eyJ[^"]+"/, '"eyJhbGciOiJIUzI1NiJ9.…"')
  }
})
</script>

<template>
  <div class="space-y-8">
    <UPageHeader
      headline="AIoT v1 · §2 §4 §5"
      :title="t('接入流程与信封校验', 'Onboarding & Envelope Inspector')"
      :description="
        t(
          '设备先拿配对码，主人在 Rodak 桌面端确认，设备轮询拿到令牌，再把令牌、MQTT、语音配置作为一个快照提交。',
          'The device gets a pairing code, the owner confirms it in Rodak desktop, the device polls for the token, then commits token, MQTT and voice as one snapshot.'
        )
      "
    />

    <!-- 绑定状态机 -->
    <UCard
      id="states"
      variant="subtle"
      :ui="{ root: 'ring-1 ring-default scroll-mt-28', body: 'space-y-5 p-5 sm:p-6' }"
    >
      <div class="flex flex-wrap items-start justify-between gap-3">
        <div class="space-y-1">
          <h2 class="text-lg font-semibold text-highlighted">
            {{ t('绑定状态机', 'Binding state machine') }}
          </h2>
          <p class="text-xs leading-relaxed text-muted">
            {{
              t(
                '点击事件推进状态。禁用按钮表示当前状态下不会发生该事件；切换 legacy / pin 模式可对比令牌刷新被拒时的行为差异。',
                'Click an event to advance the state. Disabled buttons are not valid in the current state; toggle legacy vs pin mode to compare rejected token refreshes.'
              )
            }}
          </p>
        </div>

        <div class="flex items-center gap-2">
          <div class="inline-flex rounded-lg border border-default bg-default/80 p-0.5">
            <UButton
              label="legacy"
              :color="trustMode === 'legacy' ? 'primary' : 'neutral'"
              :variant="trustMode === 'legacy' ? 'soft' : 'ghost'"
              size="xs"
              class="font-mono"
              @click="setTrustMode('legacy')"
            />
            <UButton
              label="pin"
              :color="trustMode === 'pin' ? 'primary' : 'neutral'"
              :variant="trustMode === 'pin' ? 'soft' : 'ghost'"
              size="xs"
              class="font-mono"
              @click="setTrustMode('pin')"
            />
          </div>
          <UButton
            :label="t('重置', 'Reset')"
            icon="i-lucide-rotate-ccw"
            color="neutral"
            variant="outline"
            size="xs"
            @click="resetBinding"
          />
        </div>
      </div>

      <!-- 状态进度条 -->
      <div class="flex flex-wrap items-center gap-2">
        <UBadge
          v-for="name in STATES"
          :key="name"
          :label="name"
          :color="bindingState === name ? 'primary' : 'neutral'"
          :variant="bindingState === name ? 'solid' : 'outline'"
          size="md"
          class="rounded-full px-3 font-mono"
        />
      </div>

      <!-- 可触发事件按钮 -->
      <div class="flex flex-wrap gap-2">
        <UButton
          v-for="[id, zh, en] in EVENTS"
          :key="id"
          :label="t(zh, en)"
          :disabled="!allowedEvents.has(id)"
          :color="allowedEvents.has(id) ? 'primary' : 'neutral'"
          :variant="allowedEvents.has(id) ? 'soft' : 'outline'"
          size="xs"
          class="rounded-full"
          @click="triggerEvent(id)"
        />
      </div>

      <UAlert
        icon="i-lucide-info"
        color="primary"
        variant="subtle"
        :title="`${t('当前状态', 'Current state')}: ${bindingState} (${trustMode})`"
        :description="stateNoteText"
      />

      <!-- 契约 §5 迁移表 -->
      <div class="space-y-2 pt-2">
        <h3 class="text-sm font-semibold text-highlighted">
          {{ t('契约 §5 迁移表', 'Contract §5 transitions') }}
        </h3>
        <div class="overflow-x-auto rounded-lg border border-default">
          <table class="w-full border-collapse text-left text-xs">
            <thead>
              <tr class="border-b border-default bg-elevated/70 text-muted">
                <th class="px-3 py-2 font-semibold">{{ t('起点', 'From') }}</th>
                <th class="px-3 py-2 font-semibold">{{ t('事件', 'Event') }}</th>
                <th class="px-3 py-2 font-semibold">{{ t('终点', 'To') }}</th>
                <th class="px-3 py-2 font-semibold">{{ t('设备动作', 'Device action') }}</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="([from, ev, to, zh, en], idx) in TRANSITION_ROWS"
                :key="idx"
                class="border-b border-default/60 last:border-b-0"
                :class="from === bindingState ? 'bg-primary/5' : ''"
              >
                <td class="px-3 py-2 font-mono text-primary">{{ from }}</td>
                <td class="px-3 py-2 font-medium text-highlighted">{{ ev }}</td>
                <td class="px-3 py-2 font-mono text-highlighted">{{ to }}</td>
                <td class="px-3 py-2 text-muted">{{ t(zh, en) }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </UCard>

    <!-- Envelope 检查器 -->
    <UCard
      id="envelope"
      variant="subtle"
      :ui="{ root: 'ring-1 ring-default scroll-mt-28', body: 'space-y-5 p-5 sm:p-6' }"
    >
      <div class="space-y-1">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('Envelope 检查器', 'Envelope inspector') }}
        </h2>
        <p class="text-xs leading-relaxed text-muted">
          {{
            t(
              'Rodak 的业务错误也返回 HTTP 200，结果看信封里的 code（契约 §4.1）。客户端必须先查 HTTP 状态，再查 code。',
              'Rodak returns business errors with HTTP 200; inspect the envelope code (contract §4.1). Clients must check HTTP status first, then code.'
            )
          }}
        </p>
      </div>

      <div class="flex flex-wrap gap-2">
        <UButton
          v-for="preset in ENVELOPE_PRESETS"
          :key="preset.zh"
          :label="t(preset.zh, preset.en)"
          color="neutral"
          variant="outline"
          size="xs"
          class="rounded-full"
          @click="applyPreset(preset)"
        />
      </div>

      <div class="grid gap-4 sm:grid-cols-12">
        <div class="space-y-1.5 sm:col-span-3">
          <label class="block text-xs font-medium text-highlighted" for="env-http">
            {{ t('HTTP 状态', 'HTTP status') }}
          </label>
          <UInput id="env-http" v-model.number="httpStatus" type="number" class="w-full font-mono" />
        </div>
        <div class="space-y-1.5 sm:col-span-9">
          <label class="block text-xs font-medium text-highlighted" for="env-route">
            {{ t('请求路径', 'Request path') }}
          </label>
          <UInput id="env-route" v-model="requestRoute" class="w-full font-mono" />
        </div>
      </div>

      <div class="space-y-1.5">
        <label class="block text-xs font-medium text-highlighted" for="env-body">
          {{ t('响应 body', 'Response body') }}
        </label>
        <UTextarea
          id="env-body"
          v-model="responseBody"
          :rows="5"
          class="w-full font-mono"
        />
      </div>

      <UAlert
        :color="envelopeAlertColor"
        variant="subtle"
        :icon="
          envelopeAlertColor === 'success'
            ? 'i-lucide-check-circle-2'
            : envelopeAlertColor === 'warning'
              ? 'i-lucide-alert-triangle'
              : 'i-lucide-x-circle'
        "
        :title="envelopeResult.kind"
        :description="envelopeSummary"
      />

      <!-- expiresAt 陷阱对照 -->
      <USeparator />
      <div class="space-y-3">
        <h3 class="text-sm font-semibold text-highlighted">
          {{ t('同名字段陷阱：expiresAt', 'Same name, different field: expiresAt') }}
        </h3>
        <p class="text-xs leading-relaxed text-muted">
          {{
            t(
              '配对申请记录的 expiresAt 是申请过期时间（ISO 字符串）。状态变为 confirmed 时，令牌响应被合并覆盖为令牌过期时间（Unix 秒），类型与含义同时变化。请以是否存在 accessToken 为准，并优先读取 expires_at 或 expiresIn。',
              'In a pending pairing record, expiresAt is an ISO string for request expiry. On confirmed, token fields merge over it as Unix seconds. Check for accessToken and prefer expires_at or expiresIn.'
            )
          }}
        </p>

        <div class="grid gap-4 md:grid-cols-2">
          <div class="space-y-2 rounded-lg border border-default bg-default/75 p-3.5">
            <UBadge label="binding-status.pending.json" color="neutral" variant="subtle" size="xs" class="font-mono" />
            <pre class="overflow-x-auto font-mono text-xs leading-relaxed text-toned">{{ expPendingJson || '...' }}</pre>
          </div>
          <div class="space-y-2 rounded-lg border border-default bg-default/75 p-3.5">
            <UBadge label="binding-status.confirmed.json" color="primary" variant="subtle" size="xs" class="font-mono" />
            <pre class="overflow-x-auto font-mono text-xs leading-relaxed text-toned">{{ expConfirmedJson || '...' }}</pre>
          </div>
        </div>
      </div>
    </UCard>
  </div>
</template>
