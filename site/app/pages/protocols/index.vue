<script setup lang="ts">
import { SITE_LINKS } from '~/config/links'

definePageMeta({
  layout: 'protocols'
})

const { t } = useProtocolFixtures()

useSeoMeta({
  title: () => `${t('协议交互实验室', 'Protocol Lab')} · RodakOS`,
  description: () =>
    t(
      'Rodak AIoT v1 与实时语音 v1 协议的交互式浏览器实验室，零后端直接验证状态机、信封、重放账本、RAV1 音频帧与 RSA-2048 OTA 签名。',
      'Interactive browser lab for Rodak AIoT v1 and Realtime Voice v1: test binding state machines, envelopes, replay ledgers, RAV1 frames, and RSA-2048 OTA signatures locally.'
    )
})

const channels = computed(() => [
  {
    name: 'HTTP(S)',
    icon: 'i-lucide-globe',
    accentClass: 'border-t-sky-500',
    badge: t('按需请求', 'On demand'),
    summary: t(
      '身份引导、配对与 SRP6a 建信、到期前后台令牌刷新、消息中心快照/动作、OTA 下载、解绑',
      'Bootstrap, pairing & SRP6a trust setup, background token refresh before expiry, inbox snapshots/actions, OTA download, unbind'
    )
  },
  {
    name: 'MQTT(S)',
    icon: 'i-lucide-radio',
    accentClass: 'border-t-teal-500',
    badge: t('绑定后常驻连接', 'Usually open after binding'),
    summary: t(
      '遥测、设备影子、命令与 64 条防重放回执、音量/灯光 Effect、OTA 通知、WebRTC 信令与 inbox/changed',
      'Telemetry, device shadow, commands with 64-entry replay ledger, volume/light effects, OTA notify, WebRTC signaling & inbox/changed'
    )
  },
  {
    name: 'WebSocket (WSS)',
    icon: 'i-lucide-audio-waveform',
    accentClass: 'border-t-violet-500',
    badge: t('仅唤醒或播报时打开', 'Only on wake or announce'),
    summary: t(
      'rodak-realtime-voice/v1 实时语音会话、RAV1 二进制音频帧、四重门控与按帧同步回复字幕',
      'rodak-realtime-voice/v1 voice session, RAV1 binary audio frames, four stale-result gates & frame-synced captions'
    )
  }
])

const lifecycleSteps = computed(() => [
  { label: t('① 开机 · 配对', '① Boot · pair'), to: '/protocols/onboarding' },
  { label: t('② 读懂响应', '② Read responses'), to: '/protocols/onboarding#envelope' },
  { label: t('③ 上线 · 被控制', '③ Online · controlled'), to: '/protocols/mqtt' },
  { label: t('④ 对话', '④ Converse'), to: '/protocols/voice' },
  { label: t('⑤ 播报', '⑤ Announce'), to: '/protocols/voice#announce' },
  { label: t('⑥ 升级', '⑥ Upgrade'), to: '/protocols/ota' },
  { label: t('⑦ 解绑', '⑦ Unbind'), to: '/protocols/onboarding#states' }
])

const labs = computed(() => [
  {
    title: t('绑定状态机', 'Binding state machine'),
    description: t(
      '点事件推进状态，直观对比 legacy 与 pin 模式在令牌被拒时的行为差异。',
      'Click events to move states and compare legacy vs pin mode when token refresh is rejected.'
    ),
    icon: 'i-lucide-git-branch',
    to: '/protocols/onboarding#states'
  },
  {
    title: t('Envelope 检查器', 'Envelope inspector'),
    description: t(
      '现场验证为什么 HTTP 200 不等于业务成功，以及 Appearance 路由的例外规则。',
      'Test why HTTP 200 does not guarantee success and inspect Appearance route exceptions.'
    ),
    icon: 'i-lucide-mail-check',
    to: '/protocols/onboarding#envelope'
  },
  {
    title: t('64 条命令重放账本', '64-entry command replay'),
    description: t(
      '模拟同号同字节重放、多一个空格触发冲突、挤出窗口与 64 KiB 墓碑行为。',
      'Simulate identical-byte replay, single-space conflict, window eviction, and 64 KiB tombstones.'
    ),
    icon: 'i-lucide-history',
    to: '/protocols/mqtt#replay'
  },
  {
    title: t('证据等级 L0–L4', 'Evidence levels L0–L4'),
    description: t(
      '逐级拆解每个信号能证明什么、不能证明什么，并对照真实契约 Fixture。',
      'Inspect what each evidence level proves and cannot prove against live contract fixtures.'
    ),
    icon: 'i-lucide-layers',
    to: '/protocols/mqtt#evidence'
  },
  {
    title: t('RAV1 拆帧与四道门控', 'RAV1 parser & four gates'),
    description: t(
      '按字节高亮拆解 12+N 二进制音频帧，随时注入陈旧消息查看拦截门控。',
      'Inspect byte-highlighted 12+N binary frames and inject stale messages to test all four gates.'
    ),
    icon: 'i-lucide-audio-waveform',
    to: '/protocols/voice'
  },
  {
    title: t('OTA RSA-2048 浏览器验签', 'OTA RSA-2048 browser check'),
    description: t(
      '实时构造 8 行签名原文，调用浏览器 WebCrypto 对真实 Manifest v2 夹具验签。',
      'Build the 8-line signing string and verify signed Manifest v2 fixtures via WebCrypto.'
    ),
    icon: 'i-lucide-shield-check',
    to: '/protocols/ota#signature'
  }
])
</script>

<template>
  <div class="space-y-8">
    <UPageHeader
      headline="rodak-aiot/v1 · rodak-realtime-voice/v1"
      :title="t('一台设备如何接入 Rodak', 'How a device connects to Rodak')"
      :description="
        t(
          '给要接入 Rodak 服务器、或要做兼容设备和模拟器的开发者。跟着一台设备从开机到解绑，每一步都能在浏览器里直接动手试。',
          'For developers integrating with a Rodak server or building compatible devices and simulators. Follow a device from boot to unbind and try every rule live in the browser.'
        )
      "
      :links="[
        {
          label: t('阅读 14 篇协议手册', 'Read 14-Chapter Handbook'),
          icon: 'i-lucide-book-open',
          to: SITE_LINKS.docsHandbook,
          color: 'primary',
          variant: 'soft',
          size: 'sm'
        },
        {
          label: t('浏览全部 Fixtures', 'Browse Fixtures'),
          icon: 'i-lucide-file-json-2',
          to: '/protocols/fixtures',
          color: 'neutral',
          variant: 'outline',
          size: 'sm'
        }
      ]"
    />

    <!-- 三条通道，一个令牌 -->
    <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'space-y-4 p-5 sm:p-6' }">
      <div class="flex flex-wrap items-center justify-between gap-2">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('三条通道，一个令牌', 'Three channels, one token') }}
        </h2>
        <UBadge label="HS256 JWT" color="primary" variant="subtle" size="xs" class="font-mono" />
      </div>

      <div class="grid gap-4 md:grid-cols-3">
        <div
          v-for="ch in channels"
          :key="ch.name"
          class="flex flex-col justify-between rounded-xl border border-default border-t-4 bg-default/80 p-4"
          :class="ch.accentClass"
        >
          <div class="space-y-2">
            <div class="flex items-center justify-between gap-2">
              <span class="inline-flex items-center gap-2 font-mono text-sm font-semibold text-highlighted">
                <UIcon :name="ch.icon" class="size-4 text-primary" />
                {{ ch.name }}
              </span>
              <UBadge :label="ch.badge" color="neutral" variant="subtle" size="xs" />
            </div>
            <p class="text-xs leading-relaxed text-muted">
              {{ ch.summary }}
            </p>
          </div>
        </div>
      </div>

      <div
        class="flex flex-wrap items-center justify-center gap-2 rounded-lg border border-dashed border-default bg-default/60 px-4 py-2.5 text-center text-xs text-muted"
      >
        <UIcon name="i-lucide-key-round" class="size-4 shrink-0 text-primary" />
        <span>
          {{
            t(
              '三条通道共用配对后签发的 access token（HS256 JWT）：MQTT 密码 · HTTP Bearer · 实时语音 Bearer',
              'All three channels share the access token (HS256 JWT) issued after pairing: MQTT password · HTTP Bearer · Voice Bearer'
            )
          }}
        </span>
      </div>
    </UCard>

    <!-- 设备的一生 -->
    <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'space-y-3 p-5 sm:p-6' }">
      <h2 class="text-lg font-semibold text-highlighted">
        {{ t('设备的一生', "A device's life") }}
      </h2>
      <div class="flex flex-wrap gap-2">
        <UButton
          v-for="step in lifecycleSteps"
          :key="step.to"
          :label="step.label"
          :to="step.to"
          color="neutral"
          variant="outline"
          size="sm"
          class="rounded-full"
        />
      </div>
    </UCard>

    <!-- 动手试：6 个交互实验室 -->
    <div class="space-y-4">
      <div class="flex items-center justify-between gap-2">
        <h2 class="text-lg font-semibold text-highlighted">
          {{ t('动手试 · 交互式验证工具', 'Try it · Interactive Labs') }}
        </h2>
        <span class="font-mono text-xs text-muted">Zero backend · Client-side WebCrypto</span>
      </div>

      <div class="grid gap-4 md:grid-cols-2 lg:grid-cols-3">
        <UPageCard
          v-for="lab in labs"
          :key="lab.to"
          :title="lab.title"
          :description="lab.description"
          :icon="lab.icon"
          :to="lab.to"
          variant="subtle"
          spotlight
          spotlight-color="primary"
        />
      </div>
    </div>

    <UAlert
      icon="i-lucide-book-open"
      color="primary"
      variant="subtle"
      :title="t('想系统从头通读？', 'Want to read from start to finish?')"
      :description="
        t(
          '原来的单篇静态导读（article.html）已升级合并为站内 14 篇《Rodak 协议开发者上手手册》，提供交互式时序/状态图示、代码高亮与全文检索。',
          'The former standalone article.html has been merged into the 14-chapter Protocol Developer Handbook under Docs, complete with interactive sequence/state diagrams and full-text search.'
        )
      "
      :actions="[
        {
          label: t('打开 14 篇协议手册', 'Open Handbook Index'),
          to: SITE_LINKS.docsHandbook,
          color: 'primary',
          variant: 'soft',
          size: 'xs'
        },
        {
          label: t('规范与契约参考', 'Normative Reference'),
          to: '/protocols/reference',
          color: 'neutral',
          variant: 'outline',
          size: 'xs'
        }
      ]"
    />
  </div>
</template>
