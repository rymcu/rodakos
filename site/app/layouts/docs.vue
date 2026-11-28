<script setup lang="ts">
import type { ContentNavigationItem } from '@nuxt/content'
import type { Ref } from 'vue'

const navigation = inject<Ref<ContentNavigationItem[] | null>>('docs-navigation')

const sidebarNavigation = computed<ContentNavigationItem[]>(() => {
  const items = navigation?.value ?? []
  if (items.length === 1 && items[0]?.path === '/docs' && items[0].children?.length) {
    return items[0].children
  }
  return items
})
</script>

<template>
  <UContainer>
    <UPage>
      <template #left>
        <UPageAside :ui="{ root: 'lg:pr-6' }">
          <UContentNavigation
            :navigation="sidebarNavigation"
            highlight
            default-open
          />
        </UPageAside>
      </template>

      <slot />
    </UPage>
  </UContainer>
</template>
