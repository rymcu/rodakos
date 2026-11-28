<script setup lang="ts">
import type { TabsItem } from '@nuxt/ui'
import type { HeroPreviewTabId, ScreenSceneId } from '~/types/landing'

const { copy } = useLandingCopy()

const activeTab = shallowRef<HeroPreviewTabId>('home')

const tabItems = computed<TabsItem[]>(() =>
  copy.value.hero.previewTabs.map((tab) => ({
    label: tab.label,
    icon: tab.icon,
    value: tab.id
  }))
)

function handleSyncActive(id: ScreenSceneId): void {
  if (copy.value.hero.previewTabs.some((tab) => tab.id === id)) {
    activeTab.value = id as HeroPreviewTabId
  }
}
</script>

<template>
  <div class="mx-auto w-full max-w-4xl space-y-4">
    <div class="flex max-w-full justify-start overflow-x-auto pb-1 sm:justify-center sm:pb-0">
      <UTabs
        v-model="activeTab"
        :items="tabItems"
        :content="false"
        size="sm"
        color="primary"
        variant="pill"
        :ui="{
          root: 'w-max shrink-0',
          list: 'w-max flex-nowrap',
          trigger: 'shrink-0 whitespace-nowrap'
        }"
      />
    </div>

    <LandingRealScreenFrame
      :items="copy.hero.previewTabs"
      :active-id="activeTab"
      eager
      @update:active-id="handleSyncActive"
    />

    <p class="text-center text-xs text-dimmed">
      {{ copy.hero.showcase.demoDataNote }}
    </p>
  </div>
</template>
