<script setup lang="ts">
import { SITE_LINKS } from '~/config/links'

const route = useRoute()
const { t } = useProtocolFixtures()

const tabs = computed(() => [
  { label: t('总览', 'Overview'), to: '/protocols', icon: 'i-lucide-compass', exact: true },
  { label: t('接入流程', 'Onboarding'), to: '/protocols/onboarding', icon: 'i-lucide-key-round' },
  { label: t('统一 MQTT', 'MQTT'), to: '/protocols/mqtt', icon: 'i-lucide-radio' },
  { label: t('实时语音', 'Voice'), to: '/protocols/voice', icon: 'i-lucide-audio-waveform' },
  { label: t('OTA 验签', 'OTA'), to: '/protocols/ota', icon: 'i-lucide-shield-check' },
  { label: 'Fixtures', to: '/protocols/fixtures', icon: 'i-lucide-file-json-2' },
  { label: t('规范参考', 'Reference'), to: '/protocols/reference', icon: 'i-lucide-book-marked' }
])

function isActive(to: string, exact?: boolean): boolean {
  const clean = route.path.replace(/\/$/, '') || '/'
  if (exact) return clean === to
  return clean === to || clean.startsWith(`${to}/`)
}
</script>

<template>
  <div class="min-h-full">
    <div class="sticky top-(--ui-header-height) z-30 border-b border-default bg-default/85 backdrop-blur-md">
      <UContainer class="flex flex-wrap items-center justify-between gap-2 py-2.5">
        <nav
          aria-label="Protocol Lab sections"
          class="flex max-w-full items-center gap-1 overflow-x-auto pb-0.5 sm:pb-0"
        >
          <UButton
            v-for="item in tabs"
            :key="item.to"
            :label="item.label"
            :icon="item.icon"
            :to="item.to"
            :color="isActive(item.to, item.exact) ? 'primary' : 'neutral'"
            :variant="isActive(item.to, item.exact) ? 'soft' : 'ghost'"
            size="xs"
            class="shrink-0 whitespace-nowrap"
          />
        </nav>

        <div class="hidden items-center gap-2 lg:flex">
          <UButton
            :label="t('14 篇协议手册', 'Protocol Handbook')"
            icon="i-lucide-book-open"
            trailing-icon="i-lucide-arrow-right"
            :to="SITE_LINKS.docsHandbook"
            color="neutral"
            variant="outline"
            size="xs"
          />
        </div>
      </UContainer>
    </div>

    <UContainer class="py-10 sm:py-12">
      <slot />

      <div
        class="mt-12 flex flex-col gap-3 rounded-xl border border-default bg-elevated/35 px-4 py-3 text-xs text-muted sm:flex-row sm:items-center sm:justify-between"
      >
        <span>
          {{
            t(
              'RodakOS 协议交互实验室 · 契约文件是权威来源；本站与契约冲突时以契约为准。',
              'RodakOS Protocol Lab · Contract files are authoritative; if this lab and a contract disagree, the contract wins.'
            )
          }}
        </span>
        <div class="flex flex-wrap items-center gap-3">
          <NuxtLink
            :to="SITE_LINKS.docsHandbook"
            class="font-medium text-primary hover:underline"
          >
            {{ t('阅读 14 篇协议开发者手册 →', 'Read 14-Chapter Handbook →') }}
          </NuxtLink>
          <a
            :href="SITE_LINKS.contracts"
            target="_blank"
            rel="noopener noreferrer"
            class="inline-flex items-center gap-1 font-mono text-primary hover:underline"
          >
            contracts/
            <UIcon name="i-lucide-arrow-up-right" class="size-3.5" />
          </a>
        </div>
      </div>
    </UContainer>
  </div>
</template>
