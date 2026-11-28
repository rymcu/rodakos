<script setup lang="ts">
import {
  emptyLedger,
  submitCommand,
  type ReplayLedgerState,
  type ReplayOutcome
} from '../../../shared/protocol-logic.mjs'

definePageMeta({
  layout: 'protocols'
})

const { t, getFixture } = useProtocolFixtures()

useSeoMeta({
  title: () => `${t('统一 MQTT · 协议实验室', 'Unified MQTT · Protocol Lab')} · RodakOS`,
  description: () =>
    t(
      '交互式 MQTT 主题浏览器、64 条防重放命令账本与墓碑模拟器，以及 L0–L4 证据等级阶梯与真实夹具对照。',
      'Interactive MQTT topic browser, 64-entry replay ledger & tombstone simulator, and L0–L4 evidence levels with live contract fixtures.'
    )
})

const deviceKey = shallowRef('aa:bb:cc:dd:ee:ff')

const TOPIC_ROWS = [
  { topic: 'telemetry', dir: 'up', qos: '0', schema: 'telemetry' },
  { topic: 'shadow/report', dir: 'up', qos: '0', schema: 'shadow-report' },
  { topic: 'ota/progress', dir: 'up', qos: '0 *', schema: 'ota-progress' },
  { topic: 'effects/receipt', dir: 'up', qos: '0', schema: 'effect-receipt' },
  { topic: 'commands/{commandNo}/ack', dir: 'up', qos: '0', schema: 'command-ack' },
  { topic: 'shadow/desired', dir: 'down', qos: '0', schema: 'shadow-desired' },
  { topic: 'ota/notify', dir: 'down', qos: '0', schema: 'ota-notify' },
  { topic: 'commands/{commandNo}', dir: 'down', qos: '0', schema: 'command-request' },
  { topic: 'inbox/changed', dir: 'down', qos: '0', schema: 'inbox-changed (optional §7.6)' },
  { topic: 'pc_status', dir: 'down', qos: '–', schema: null }
] as const

const displayKey = computed(() => deviceKey.value.trim() || '{k}')

// 64-entry Command Replay Ledger Simulator
const ledger = ref<ReplayLedgerState>(emptyLedger())
const commandNo = shallowRef('9f3a1c2e7b4d4e6f8a1b2c3d4e5f6a7b')
const rawPayload = shallowRef('{"command":"ping"}')
const bigReply = shallowRef(false)
const replayStatus = shallowRef<{ color: 'success' | 'warning' | 'error' | 'primary'; text: string } | null>(null)

const OUTCOMES: Record<ReplayOutcome, ['success' | 'warning' | 'error', string, string]> = {
  first_execution: ['success', '首次执行：设备运行命令并缓存 ACK。', 'First execution: the device runs the command and caches the ACK.'],
  replay: ['success', '相同字节：重放缓存的 ACK，不再执行。', 'Identical bytes: replay the cached ACK without running again.'],
  command_conflict: ['error', 'command_conflict：同一个 commandNo，payload 字节不同。', 'command_conflict: same commandNo, different payload bytes.'],
  command_result_unavailable: [
    'warning',
    'command_result_unavailable：记录还在窗口里，但回复没缓存下来（墓碑）。不再执行；原命令可能已经生效。',
    'command_result_unavailable: record is in the window as a tombstone because its reply exceeded budget. Not re-run; original may have taken effect.'
  ],
  executed_after_eviction: [
    'warning',
    '再次执行：这个 commandNo 已被挤出 64 条窗口，设备把它当成新命令。被挤出的命令没有去重保证。',
    'Executed again: this commandNo was evicted from the 64-entry window and treated as new. Evicted commands have no dedup guarantee.'
  ]
}

function runCommand() {
  const res = submitCommand(ledger.value, commandNo.value, rawPayload.value, {
    replyRetained: !bigReply.value
  })
  ledger.value = res.state
  const [color, zh, en] = OUTCOMES[res.outcome]
  replayStatus.value = { color, text: t(zh, en) }
}

function appendSpace() {
  rawPayload.value += ' '
}

function generateNewCommandNo() {
  commandNo.value = crypto.randomUUID().replaceAll('-', '')
  rawPayload.value = '{"command":"ping"}'
}

function fill64Commands() {
  let next = ledger.value
  for (let i = 0; i < 64; i += 1) {
    next = submitCommand(next, crypto.randomUUID().replaceAll('-', ''), '{"command":"ping"}').state
  }
  ledger.value = next
  replayStatus.value = {
    color: 'primary',
    text: t(
      '已发送 64 条新命令，原来的命令已被挤出窗口。现在再点一次「发送」观察结果。',
      'Sent 64 new commands; the earlier entry was pushed out of the window. Send the original commandNo again to observe.'
    )
  }
}

function clearLedger() {
  ledger.value = emptyLedger()
  replayStatus.value = {
    color: 'warning',
    text: t(
      '重启后窗口清空：同一个 commandNo 会被当成新命令再执行一次。因此服务端必须为每条新命令生成新的 commandNo。',
      'After a reboot the volatile window is empty: the same commandNo runs again as a new command. Servers must generate fresh commandNo values.'
    )
  }
}

const shortRaw = (s: string) => (s.length > 38 ? `${s.slice(0, 38)}…` : s)

const ledgerPreview = computed(() => {
  const entries = ledger.value.entries
  if (!entries.length) return t('（空）', '(empty)')
  const lines = entries
    .slice(-8)
    .map(
      (x) =>
        `${x.commandNo.slice(0, 12)}  ${shortRaw(x.raw)}${x.retained ? '' : t('  [墓碑]', '  [tombstone]')}`
    )
  return `${entries.length} / 64\n${entries.length > 8 ? '…\n' : ''}${lines.join('\n')}`
})

const evictedPreview = computed(() => {
  const evicted = ledger.value.evicted
  if (!evicted.length) return t('（无）', '(none)')
  return `${evicted.length}\n${evicted.slice(-8).map((x) => x.slice(0, 12)).join('\n')}`
})

// Evidence Levels L0-L4
const LEVELS = [
  {
    id: 'L0',
    tZh: 'MQTT 发布完成',
    tEn: 'MQTT publish completes',
    pZh: 'SDK 接受了消息。',
    pEn: 'The SDK accepted the message.',
    nZh: 'Broker 或设备收到了它。',
    nEn: 'That the broker or device received it.',
    fixtureName: null as string | null
  },
  {
    id: 'L1',
    tZh: '命令 ACK ok',
    tEn: 'Command ACK ok',
    pZh: '设备接纳并接受了命令。',
    pEn: 'The device admitted and accepted the command.',
    nZh: '命令完成、开始播放或产生了物理效果。',
    nEn: 'Completion, playback, or any physical effect.',
    fixtureName: 'command-ack.announce-accepted.json'
  },
  {
    id: 'L2',
    tZh: '上报影子的值',
    tEn: 'Reported shadow value',
    pZh: '设备当前接受的配置。',
    pEn: "The device's current accepted configuration.",
    nZh: '是某一次期望修改导致了这个值。',
    nEn: 'That a specific desired change caused this value.',
    fixtureName: 'shadow-report.full.json'
  },
  {
    id: 'L3',
    tZh: '效果回执',
    tEn: 'Effect receipt',
    pZh: '一次关联的效果到达了软件中的某个阶段。',
    pEn: 'One correlated effect reached a stated software stage.',
    nZh: '物理输出。回执里 physicalVerified 必须是 false。',
    nEn: 'Physical output. physicalVerified must be false.',
    fixtureName: 'effect-receipt.volume.json'
  },
  {
    id: 'L4',
    tZh: '硬件观测',
    tEn: 'Hardware observation',
    pZh: '物理效果（如独立传感器或实测闭环）。',
    pEn: 'Observed physical effect.',
    nZh: '—',
    nEn: '—',
    fixtureName: null as string | null
  }
] as const

const activeLevelIdx = shallowRef(3)
const levelFixtureJson = shallowRef<string | null>(null)
const activeLevel = computed(() => LEVELS[activeLevelIdx.value] ?? LEVELS[3]!)

async function loadLevelFixture() {
  const name = activeLevel.value.fixtureName
  if (!name) {
    levelFixtureJson.value = null
    return
  }
  const content = await getFixture(name)
  levelFixtureJson.value = content !== null ? JSON.stringify(content, null, 2) : null
}

watch(activeLevelIdx, () => {
  void loadLevelFixture()
})

onMounted(() => {
  void loadLevelFixture()
})
</script>

<template>
  <div class="space-y-8">
    <UPageHeader
      headline="AIoT v1 · §7 §8 §9 §10"
      :title="t('统一 MQTT、重放账本与证据阶梯', 'Unified MQTT, Replay Ledger & Evidence')"
      :description="
        t(
          '设备用 device key 作用户名、access token 作密码连接 Broker。状态走影子，动作走命令，每个信号只证明它自己那一级。',
          'The device connects with device key as username and access token as password. State travels in shadow, actions in commands, and each signal proves only its own level.'
        )
      "
    />

    <!-- 主题浏览器 -->
    <UCard
      id="topics"
      variant="subtle"
      :ui="{ root: 'ring-1 ring-default scroll-mt-28', body: 'space-y-4 p-5 sm:p-6' }"
    >
      <div class="flex flex-wrap items-end justify-between gap-4">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('主题浏览器', 'Topic browser') }}
        </h2>
        <div class="w-full max-w-xs space-y-1">
          <label class="block text-xs font-medium text-muted" for="mqtt-key">deviceKey</label>
          <UInput id="mqtt-key" v-model="deviceKey" class="w-full font-mono" size="sm" />
        </div>
      </div>

      <div class="overflow-x-auto rounded-lg border border-default">
        <table class="w-full border-collapse text-left text-xs">
          <thead>
            <tr class="border-b border-default bg-elevated/70 text-muted">
              <th class="px-3 py-2 font-semibold">{{ t('主题', 'Topic') }}</th>
              <th class="px-3 py-2 font-semibold">{{ t('方向', 'Direction') }}</th>
              <th class="px-3 py-2 font-semibold">QoS</th>
              <th class="px-3 py-2 font-semibold">Retain</th>
              <th class="px-3 py-2 font-semibold">Payload</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="row in TOPIC_ROWS"
              :key="row.topic"
              class="border-b border-default/60 last:border-b-0"
            >
              <td class="px-3 py-2 font-mono text-primary">
                devices/{{ displayKey }}/{{ row.topic }}
              </td>
              <td class="px-3 py-2">
                <UBadge
                  :label="
                    row.dir === 'up'
                      ? t('设备 → 服务器', 'device → server')
                      : t('服务器 → 设备', 'server → device')
                  "
                  :color="row.dir === 'up' ? 'primary' : 'neutral'"
                  variant="subtle"
                  size="xs"
                />
              </td>
              <td class="px-3 py-2 font-mono text-highlighted">{{ row.qos }}</td>
              <td class="px-3 py-2 text-muted">
                {{ row.topic === 'pc_status' ? '–' : t('否', 'no') }}
              </td>
              <td class="px-3 py-2">
                <code v-if="row.schema" class="font-mono text-toned">{{ row.schema }}</code>
                <span v-else class="text-muted">
                  {{ t('无内置生产者，静默正常', 'No built-in producer; silence is normal') }}
                </span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      <div class="space-y-1.5 text-xs leading-relaxed text-muted">
        <p>
          * {{ t('RodakOS 仅在 stepCode: staged 时使用 QoS 1。', 'RodakOS uses QoS 1 only for stepCode: staged.') }}
        </p>
        <p>
          {{
            t(
              '设备订阅 shadow/desired、commands/+、ota/notify、pc_status（以及仅在服务端明确下发 topics.inbox_changed 时才订阅的 inbox/changed）。重连后重新订阅即触发一次快照/待处理命令投递，无需轮询。',
              'The device subscribes to shadow/desired, commands/+, ota/notify, pc_status (plus inbox/changed only when listed in unifiedMqtt.topics). Re-subscribing after reconnect triggers one QoS 0 snapshot/pending delivery.'
            )
          }}
        </p>
      </div>
    </UCard>

    <!-- 64 条命令重放模拟器 -->
    <UCard
      id="replay"
      variant="subtle"
      :ui="{ root: 'ring-1 ring-default scroll-mt-28', body: 'space-y-5 p-5 sm:p-6' }"
    >
      <div class="space-y-1">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('命令重放模拟器（64 条账本）', 'Command replay simulator (64-entry ledger)') }}
        </h2>
        <p class="text-xs leading-relaxed text-muted">
          {{
            t(
              '设备为每个服务器身份保留最近 64 个 commandNo。同号同字节重放原 ACK；同号不同字节（哪怕多一个空格）返回 command_conflict；挤出窗口无去重保证；回复超 64 KiB 预算留墓碑并返回 command_result_unavailable。',
              'Devices keep the latest 64 commandNo entries per authority. Identical bytes replay ACK; any byte change triggers command_conflict; evicted entries run again; oversized replies leave a tombstone returning command_result_unavailable.'
            )
          }}
        </p>
      </div>

      <div class="grid gap-4 sm:grid-cols-2">
        <div class="space-y-1.5">
          <label class="block text-xs font-medium text-highlighted" for="cmd-no">commandNo</label>
          <UInput id="cmd-no" v-model="commandNo" class="w-full font-mono" />
        </div>
        <div class="space-y-1.5">
          <label class="block text-xs font-medium text-highlighted" for="cmd-payload">
            {{ t('payload 原始字节', 'Raw payload bytes') }}
          </label>
          <UInput id="cmd-payload" v-model="rawPayload" class="w-full font-mono" />
        </div>
      </div>

      <div class="flex flex-wrap items-center gap-2.5">
        <UButton :label="t('发送', 'Send')" icon="i-lucide-send" color="primary" size="sm" @click="runCommand" />
        <UCheckbox
          v-model="bigReply"
          :label="t('回复放不进 64 KiB 缓存（产生墓碑）', 'Reply exceeds 64 KiB budget (tombstone)')"
        />
        <UButton :label="t('payload 末尾加空格', 'Append space')" color="neutral" variant="outline" size="xs" @click="appendSpace" />
        <UButton :label="t('换新 commandNo', 'New commandNo')" color="neutral" variant="outline" size="xs" @click="generateNewCommandNo" />
        <UButton :label="t('再发 64 条（挤出窗口）', 'Send 64 more (evict)')" color="neutral" variant="outline" size="xs" @click="fill64Commands" />
        <UButton :label="t('清空（模拟重启）', 'Clear (reboot)')" color="neutral" variant="ghost" size="xs" @click="clearLedger" />
      </div>

      <UAlert
        v-if="replayStatus"
        :color="replayStatus.color"
        variant="subtle"
        :icon="
          replayStatus.color === 'success'
            ? 'i-lucide-check-circle-2'
            : replayStatus.color === 'error'
              ? 'i-lucide-x-circle'
              : 'i-lucide-alert-triangle'
        "
        :description="replayStatus.text"
      />

      <div class="grid gap-4 md:grid-cols-2">
        <div class="space-y-2 rounded-lg border border-default bg-default/75 p-3.5">
          <h3 class="text-xs font-semibold text-highlighted">
            {{ t('窗口（最近 64 条）', 'Window (latest 64)') }}
          </h3>
          <pre class="overflow-x-auto font-mono text-xs leading-relaxed text-toned">{{ ledgerPreview }}</pre>
        </div>
        <div class="space-y-2 rounded-lg border border-default bg-default/75 p-3.5">
          <h3 class="text-xs font-semibold text-highlighted">
            {{ t('已被挤出窗口', 'Evicted from window') }}
          </h3>
          <pre class="overflow-x-auto font-mono text-xs leading-relaxed text-toned">{{ evictedPreview }}</pre>
        </div>
      </div>
    </UCard>

    <!-- 证据等级 L0-L4 -->
    <UCard
      id="evidence"
      variant="subtle"
      :ui="{ root: 'ring-1 ring-default scroll-mt-28', body: 'space-y-5 p-5 sm:p-6' }"
    >
      <div class="space-y-1">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('证据等级：每个信号只证明它自己', 'Evidence levels: each signal proves only itself') }}
        </h2>
        <p class="text-xs leading-relaxed text-muted">
          {{
            t(
              '点击任意等级查看其证明边界。高等级不能由低等级推出；固件目前对音量和灯光最高声明到 L3。',
              'Select a level to see what it proves and cannot prove. Higher levels never follow from lower ones; firmware claims at most L3 for volume and lights today.'
            )
          }}
        </p>
      </div>

      <div class="flex flex-wrap gap-2">
        <UButton
          v-for="(lv, idx) in LEVELS"
          :key="lv.id"
          :label="`${lv.id} · ${t(lv.tZh, lv.tEn)}`"
          :color="activeLevelIdx === idx ? 'primary' : 'neutral'"
          :variant="activeLevelIdx === idx ? 'solid' : 'outline'"
          size="xs"
          class="rounded-full"
          @click="activeLevelIdx = idx"
        />
      </div>

      <div class="grid gap-4 md:grid-cols-2">
        <UAlert
          color="success"
          variant="subtle"
          icon="i-lucide-check-circle-2"
          :title="t('能证明', 'Proves')"
          :description="t(activeLevel.pZh, activeLevel.pEn)"
        />
        <UAlert
          color="error"
          variant="subtle"
          icon="i-lucide-shield-off"
          :title="t('不能证明', 'Does not prove')"
          :description="t(activeLevel.nZh, activeLevel.nEn)"
        />
      </div>

      <div
        v-if="activeLevel.fixtureName"
        class="space-y-2 rounded-lg border border-default bg-default/75 p-3.5"
      >
        <UBadge :label="activeLevel.fixtureName" color="success" variant="subtle" size="xs" class="font-mono" />
        <pre class="overflow-x-auto font-mono text-xs leading-relaxed text-toned">{{ levelFixtureJson || '...' }}</pre>
      </div>
      <p v-else class="text-xs text-muted">
        {{ t('没有对应的 fixture：这一级不是线上报文。', 'No fixture: this level is not a wire message.') }}
      </p>
    </UCard>
  </div>
</template>
