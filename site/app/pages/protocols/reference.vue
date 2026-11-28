<script setup lang="ts">
import { SITE_LINKS } from '~/config/links'

definePageMeta({
  layout: 'protocols'
})

const { t } = useProtocolFixtures()
const { copiedKey, copyText } = useClipboardCopy()

useSeoMeta({
  title: () => `${t('规范与契约参考 · 协议实验室', 'Normative Reference · Protocol Lab')} · RodakOS`,
  description: () =>
    t(
      '直达 Rodak AIoT v1 与实时语音 v1 的权威规范文档、OpenAPI/AsyncAPI/JSON Schema 契约文件以及最小模拟器实现清单。',
      'Direct links to normative Rodak AIoT v1 & Realtime Voice v1 contracts, OpenAPI/AsyncAPI/JSON Schema assets, and minimal simulator steps.'
    )
})

const normativeDocs = computed(() => [
  {
    title: 'Rodak AIoT v1',
    description: t('身份、配对、凭据、统一 MQTT、消息中心、影子、命令与 OTA。', 'Identity, pairing, credentials, unified MQTT, inbox, shadow, commands & OTA.'),
    icon: 'i-lucide-file-text',
    to: 'https://github.com/rymcu/rodakos/blob/main/docs/rodak-aiot-contract-v1.md'
  },
  {
    title: 'Rodak realtime voice v1',
    description: t('WebSocket 会话、RAV1 音频帧、回复字幕、门控与主动播报。', 'WebSocket session, RAV1 frames, captions, gates & server announcements.'),
    icon: 'i-lucide-audio-waveform',
    to: 'https://github.com/rymcu/rodakos/blob/main/docs/rodak-realtime-voice-contract-v1.md'
  },
  {
    title: 'MQTT command semantics',
    description: t('64 条重放账本、精确串流 Stop 与串流租约。', '64-entry replay ledger, exact stream Stop & stream leases.'),
    icon: 'i-lucide-history',
    to: 'https://github.com/rymcu/rodakos/blob/main/docs/mqtt-command-semantics.md'
  },
  {
    title: 'LAN onboarding (SRP6a)',
    description: t('免 USB 局域网 6 位码与 SRP-6a 可信建网设计。', 'USB-free 6-digit SRP-6a LAN trust setup design.'),
    icon: 'i-lucide-shield-check',
    to: 'https://github.com/rymcu/rodakos/blob/main/docs/lan-onboarding-design.md'
  }
])

const contractFiles = computed(() => [
  {
    label: 'rodak-aiot/v1/openapi.yaml',
    to: 'https://github.com/rymcu/rodakos/blob/main/contracts/rodak-aiot/v1/openapi.yaml',
    external: true,
    desc: t('HTTP 接口（契约 §4、§7.6、§12）', 'HTTP operations (contract §4, §7.6, §12)')
  },
  {
    label: 'rodak-aiot/v1/asyncapi.yaml',
    to: 'https://github.com/rymcu/rodakos/blob/main/contracts/rodak-aiot/v1/asyncapi.yaml',
    external: true,
    desc: t('MQTT 通道（契约 §7.3）', 'MQTT channels (contract §7.3)')
  },
  {
    label: 'rodak-aiot/v1/schemas/',
    to: 'https://github.com/rymcu/rodakos/tree/main/contracts/rodak-aiot/v1/schemas',
    external: true,
    desc: t('每种 AIoT payload 的 JSON Schema 2020-12', 'JSON Schema 2020-12 for every AIoT payload')
  },
  {
    label: 'rodak-realtime-voice/v1/schemas/',
    to: 'https://github.com/rymcu/rodakos/tree/main/contracts/rodak-realtime-voice/v1/schemas',
    external: true,
    desc: t('每个语音控制事件和 RAV1 帧的 Schema', 'Schemas for every voice control event and RAV1 frame')
  },
  {
    label: '*/examples/valid|invalid/',
    to: '/protocols/fixtures',
    external: false,
    desc: t('Schema 必须接受或拒绝的例子，可直接在 Fixtures 页浏览', 'Examples each schema must accept or reject — browse live in Fixtures')
  }
])

const simulatorSteps = computed(() => [
  t('HTTP：bootstrap → binding/request → 轮询 status 拿到令牌，在 Rodak 桌面端输入配对码。', 'HTTP: bootstrap → binding/request → poll status until confirmed after owner enters the pairing code.'),
  t('先实现信封两步解析（HTTP 状态码 + code == 200），再编写业务逻辑。', 'Implement two-step envelope parsing (HTTP status + code == 200) before business logic.'),
  t('MQTT：用 device key 与 access token 连接，订阅 shadow/desired、commands/+、ota/notify，并按需订阅 inbox/changed。', 'MQTT: connect with device key & access token, subscribe to shadow/desired, commands/+, ota/notify, and optional inbox/changed.'),
  t('命令：先实现 ping，所有未实现指令必须明确回复 unsupported_command。', 'Commands: implement ping first and reply unsupported_command for everything else.'),
  t('语音：解析描述符，实现 session.open/ready、RAV1 编解码、四道门控与可选 output.caption 协商。', 'Voice: parse the descriptor, implement session.open/ready, RAV1 framing, four gates, and optional output.caption.'),
  t('每一步都用 examples/valid 与 examples/invalid 夹具测试解析器。', 'Validate parsers against examples/valid and examples/invalid fixtures at each step.')
])

const verifyCmd = 'python3 -I tools/check_aiot_contract.py'
</script>

<template>
  <div class="space-y-8">
    <UPageHeader
      headline="Normative sources"
      :title="t('规范原文与机器可读契约', 'Normative Sources & Machine-Readable Contracts')"
      :description="
        t(
          '本站是交互式导读。规范原文与机器可读契约才是权威来源；规范与 Rodak 服务器实际行为冲突时，以服务器为准。',
          'This lab is an interactive guide. Normative documents and machine-readable contracts are authoritative; if a document and the Rodak server disagree, the server wins.'
        )
      "
      :links="[
        {
          label: t('14 篇协议开发者手册', '14-Chapter Handbook'),
          icon: 'i-lucide-book-open',
          to: SITE_LINKS.docsHandbook,
          color: 'primary',
          variant: 'soft',
          size: 'sm'
        },
        {
          label: t('第 13 篇 Python 模拟器实战', 'Ch 13 Python Simulator'),
          icon: 'i-lucide-terminal',
          to: '/docs/advanced-guides/protocol-minimal-simulator',
          color: 'neutral',
          variant: 'outline',
          size: 'sm'
        }
      ]"
    />

    <!-- 规范文档 -->
    <div class="space-y-4">
      <h2 class="text-lg font-semibold text-highlighted">
        {{ t('规范文档', 'Normative documents') }}
      </h2>
      <div class="grid gap-4 md:grid-cols-2">
        <UPageCard
          v-for="doc in normativeDocs"
          :key="doc.title"
          :title="doc.title"
          :description="doc.description"
          :icon="doc.icon"
          :to="doc.to"
          target="_blank"
          variant="subtle"
          spotlight
          spotlight-color="primary"
        />
      </div>
    </div>

    <!-- 机器可读契约 -->
    <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'space-y-5 p-5 sm:p-6' }">
      <h2 class="text-lg font-semibold text-highlighted">
        {{ t('机器可读契约资产', 'Machine-readable contracts') }}
      </h2>

      <div class="overflow-x-auto rounded-lg border border-default">
        <table class="w-full border-collapse text-left text-xs">
          <thead>
            <tr class="border-b border-default bg-elevated/70 text-muted">
              <th class="px-3 py-2 font-semibold">{{ t('文件 / 目录', 'File / Directory') }}</th>
              <th class="px-3 py-2 font-semibold">{{ t('内容', 'Content') }}</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="row in contractFiles"
              :key="row.label"
              class="border-b border-default/60 last:border-b-0"
            >
              <td class="px-3 py-2 font-mono">
                <a
                  v-if="row.external"
                  :href="row.to"
                  target="_blank"
                  rel="noopener noreferrer"
                  class="text-primary hover:underline"
                >
                  {{ row.label }}
                </a>
                <NuxtLink v-else :to="row.to" class="text-primary hover:underline">
                  {{ row.label }}
                </NuxtLink>
              </td>
              <td class="px-3 py-2 text-muted">{{ row.desc }}</td>
            </tr>
          </tbody>
        </table>
      </div>

      <div class="space-y-2">
        <h3 class="text-sm font-semibold text-highlighted">
          {{ t('跨仓库契约一键校验命令', 'One-line contract verification command') }}
        </h3>
        <div
          class="flex items-center justify-between gap-2 rounded-lg border border-default bg-default/80 px-3.5 py-2.5 font-mono text-xs text-highlighted"
        >
          <code class="overflow-x-auto">{{ verifyCmd }}</code>
          <UButton
            :icon="copiedKey === 'verify-cmd' ? 'i-lucide-check' : 'i-lucide-copy'"
            :label="copiedKey === 'verify-cmd' ? t('已复制', 'Copied') : t('复制', 'Copy')"
            color="neutral"
            variant="ghost"
            size="xs"
            @click="copyText('verify-cmd', verifyCmd)"
          />
        </div>
        <p class="text-xs text-muted">
          {{ t('一次校验全部 Fixture、Schema 引用、x-orderedFields 规则与 OTA 签名。', 'Checks every fixture, schema reference, x-orderedFields rule, and OTA signature.') }}
        </p>
      </div>
    </UCard>

    <!-- 最小模拟器实现顺序 -->
    <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'space-y-4 p-5 sm:p-6' }">
      <div class="flex flex-wrap items-center justify-between gap-2">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('最小模拟器的推荐实现顺序', 'Recommended minimal simulator order') }}
        </h2>
        <UButton
          :label="t('查看完整 Python 代码', 'Full Python walkthrough')"
          trailing-icon="i-lucide-arrow-right"
          to="/docs/advanced-guides/protocol-minimal-simulator"
          color="primary"
          variant="soft"
          size="xs"
        />
      </div>

      <ol class="space-y-2.5 text-sm text-toned">
        <li v-for="(step, idx) in simulatorSteps" :key="idx" class="flex items-start gap-3">
          <span
            class="mt-0.5 inline-flex size-5 shrink-0 items-center justify-center rounded-full bg-primary/15 font-mono text-xs font-bold text-primary"
          >
            {{ idx + 1 }}
          </span>
          <span class="leading-relaxed">{{ step }}</span>
        </li>
      </ol>
    </UCard>
  </div>
</template>
