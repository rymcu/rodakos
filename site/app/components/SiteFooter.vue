<script setup lang="ts">
import type { FooterColumn } from '@nuxt/ui'
import { SITE_LINKS } from '~/config/links'

const { copy } = useLandingCopy()
const { withBase } = useSiteAssetUrl()

const footerColumns = computed<FooterColumn[]>(() =>
  copy.value.footer.columns.map((group) => ({
    label: group.label,
    children: group.children.map((item) => ({
      label: item.label,
      to: item.external && !item.to.startsWith('http') ? withBase(item.to) : item.to,
      target: item.target,
      external: item.external
    }))
  }))
)
</script>

<template>
  <UFooter class="border-t border-default bg-default/70">
    <template #top>
      <UContainer class="py-10">
        <div class="grid gap-10 lg:grid-cols-12">
          <div class="space-y-3 lg:col-span-4">
            <AppBrandLogo />
            <p class="max-w-sm text-sm leading-relaxed text-muted">
              {{ copy.footer.tagline }}
            </p>
            <div class="flex flex-wrap items-center gap-2 pt-1">
              <UBadge label="ESP32-S3" color="neutral" variant="subtle" size="xs" class="font-mono" />
              <UBadge label="ESP-IDF 6.0.2" color="neutral" variant="subtle" size="xs" class="font-mono" />
              <UBadge label="LVGL 9.3 · Open Firmware" color="primary" variant="subtle" size="xs" />
            </div>
          </div>

          <div class="lg:col-span-8">
            <UFooterColumns :columns="footerColumns" />
          </div>
        </div>
      </UContainer>
    </template>

    <template #left>
      <p class="text-xs text-muted">
        {{ copy.footer.copyright }}
      </p>
    </template>

    <template #right>
      <div class="flex items-center gap-1">
        <UButton
          icon="i-simple-icons-github"
          color="neutral"
          variant="ghost"
          size="sm"
          :to="SITE_LINKS.github"
          target="_blank"
          aria-label="RodakOS GitHub"
        />
        <UButton
          icon="i-lucide-circuit-board"
          color="neutral"
          variant="ghost"
          size="sm"
          :to="SITE_LINKS.hardware"
          target="_blank"
          aria-label="BigSmart-Open GitHub"
        />
        <UButton
          icon="i-lucide-monitor"
          color="neutral"
          variant="ghost"
          size="sm"
          :to="SITE_LINKS.rodakReleases"
          target="_blank"
          aria-label="Rodak Desktop & NAS Releases"
        />
      </div>
    </template>
  </UFooter>
</template>
