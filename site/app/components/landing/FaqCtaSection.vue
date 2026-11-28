<script setup lang="ts">
import type { AccordionItem, ButtonProps } from '@nuxt/ui'
import { SITE_LINKS } from '~/config/links'

const { copy } = useLandingCopy()

const accordionItems = computed<AccordionItem[]>(() =>
  copy.value.faq.items.map((item) => ({
    label: item.label,
    icon: item.icon,
    content: item.content
  }))
)

const ctaLinks = computed<ButtonProps[]>(() => [
  {
    label: copy.value.faq.primaryCta,
    icon: 'i-simple-icons-github',
    to: SITE_LINKS.github,
    target: '_blank',
    color: 'primary',
    variant: 'solid',
    size: 'lg'
  },
  {
    label: copy.value.faq.secondaryCta,
    icon: 'i-lucide-book-open',
    to: SITE_LINKS.docsStart,
    color: 'neutral',
    variant: 'outline',
    size: 'lg'
  },
  {
    label: copy.value.faq.tertiaryCta,
    icon: 'i-lucide-flask-conical',
    to: SITE_LINKS.protocolsLabPath,
    color: 'neutral',
    variant: 'ghost',
    size: 'lg'
  }
])
</script>

<template>
  <div class="border-t border-muted bg-elevated/20">
    <UPageSection
      :headline="copy.faq.headline"
      :title="copy.faq.title"
      :description="copy.faq.description"
      :ui="{
        container: 'pt-16 pb-12 sm:pt-24 sm:pb-16',
        title: 'text-2xl sm:text-4xl'
      }"
    >
      <div class="mx-auto w-full max-w-3xl">
        <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'px-5 py-2 sm:px-6' }">
          <UAccordion :items="accordionItems" />
        </UCard>
      </div>
    </UPageSection>

    <UContainer class="pb-16 sm:pb-24">
      <UPageCTA
        :title="copy.faq.ctaTitle"
        :description="copy.faq.ctaDescription"
        :links="ctaLinks"
        variant="subtle"
        class="rd-hero-backdrop"
      />
    </UContainer>
  </div>
</template>
