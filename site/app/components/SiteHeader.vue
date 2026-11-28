<script setup lang="ts">
import type { NavigationMenuItem } from '@nuxt/ui'
import { SITE_LINKS } from '~/config/links'

const route = useRoute()
const { copy, toggleLocale } = useLandingCopy()

const navItems = computed<NavigationMenuItem[]>(() => [
  ...copy.value.nav.links.map((item) => ({
    label: item.label,
    to: item.to,
    active: Boolean(item.hash) && route.path === '/' && route.hash === item.hash
  })),
  {
    label: copy.value.nav.docsLabel,
    to: SITE_LINKS.docsStart,
    active: route.path.startsWith('/docs')
  },
  {
    label: copy.value.nav.protocolsLabel,
    to: SITE_LINKS.protocolsLabPath,
    active: route.path.startsWith('/protocols')
  }
])
</script>

<template>
  <UHeader
    title="RodakOS"
    to="/"
    :ui="{
      root: 'border-b border-default bg-default/80 backdrop-blur-md'
    }"
  >
    <template #title>
      <AppBrandLogo />
    </template>

    <UNavigationMenu :items="navItems" variant="link" />

    <template #right>
      <UContentSearchButton
        :label="copy.nav.searchLabel"
        variant="ghost"
        color="neutral"
        size="sm"
        class="hidden sm:inline-flex"
      />

      <UButton
        :label="copy.nav.localeToggleLabel"
        color="neutral"
        variant="ghost"
        size="sm"
        class="font-mono"
        aria-label="切换语言 / Switch language"
        @click="toggleLocale"
      />

      <UColorModeButton color="neutral" variant="ghost" size="sm" />

      <UButton
        :label="copy.nav.githubCta"
        icon="i-simple-icons-github"
        :to="SITE_LINKS.github"
        target="_blank"
        color="primary"
        variant="soft"
        size="sm"
        class="hidden md:inline-flex"
      />
    </template>

    <template #body>
      <div class="space-y-4 py-2">
        <UNavigationMenu :items="navItems" orientation="vertical" class="-mx-2.5" />
        <USeparator />
        <UButton
          :label="copy.nav.githubCta"
          icon="i-simple-icons-github"
          :to="SITE_LINKS.github"
          target="_blank"
          color="primary"
          variant="solid"
          size="sm"
          block
        />
      </div>
    </template>
  </UHeader>
</template>
