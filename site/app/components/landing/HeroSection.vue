<script setup lang="ts">
import type { ButtonProps } from '@nuxt/ui'
import { SITE_LINKS } from '~/config/links'

const { copy } = useLandingCopy()

const heroLinks = computed<ButtonProps[]>(() => [
  {
    label: copy.value.hero.primaryCta,
    icon: 'i-simple-icons-github',
    to: SITE_LINKS.github,
    target: '_blank',
    color: 'primary',
    variant: 'solid',
    size: 'lg'
  },
  {
    label: copy.value.hero.secondaryCta,
    icon: 'i-lucide-flask-conical',
    to: SITE_LINKS.protocolsLabPath,
    color: 'neutral',
    variant: 'outline',
    size: 'lg'
  },
  {
    label: copy.value.hero.docsCta,
    trailingIcon: 'i-lucide-arrow-right',
    to: SITE_LINKS.docsStart,
    color: 'neutral',
    variant: 'ghost',
    size: 'lg'
  }
])
</script>

<template>
  <div class="rd-hero-backdrop relative overflow-hidden border-b border-muted">
    <div class="rd-grid-pattern pointer-events-none absolute inset-0 opacity-60" aria-hidden="true" />

    <UPageHero
      :description="copy.hero.description"
      :ui="{
        root: 'relative z-10',
        container: 'py-16 sm:py-20 lg:py-24 gap-10 sm:gap-y-12',
        title: 'text-3xl sm:text-5xl lg:text-[3.35rem] lg:leading-[1.15]',
        description: 'max-w-2xl mx-auto text-base sm:text-lg'
      }"
    >
      <template #headline>
        <UBadge
          :label="copy.hero.badge"
          icon="i-lucide-cpu"
          color="primary"
          variant="subtle"
          size="md"
          :ui="{
            base: 'max-w-full rounded-full px-3.5 py-1.5 text-center whitespace-normal',
            label: 'whitespace-normal text-wrap'
          }"
        />
      </template>

      <template #title>
        <span class="block">{{ copy.hero.titleLine1 }}</span>
        <span class="mt-1 block text-primary">{{ copy.hero.titleLine2 }}</span>
      </template>

      <template #footer>
        <div class="space-y-4">
          <div class="flex flex-wrap items-center justify-center gap-3">
            <UButton
              v-for="link in heroLinks"
              :key="link.label"
              v-bind="link"
            />
          </div>
          <p class="text-xs text-dimmed">
            {{ copy.hero.hint }}
          </p>
        </div>
      </template>

      <LandingHeroDeviceShowcase />
    </UPageHero>
  </div>
</template>
