<script setup lang="ts">
const { withBase } = useSiteAssetUrl()
const target = withBase('docs/getting-started')

useHead({
  meta: [{ 'http-equiv': 'refresh', content: `0; url=${target}` }]
})

if (import.meta.client) {
  await navigateTo('/docs/getting-started', { replace: true })
}
</script>

<template>
  <UContainer class="py-20 text-center text-sm text-muted">
    正在前往
    <NuxtLink to="/docs/getting-started" class="font-medium text-primary underline">
      RodakOS 开发文档
    </NuxtLink>
    ...
  </UContainer>
</template>
