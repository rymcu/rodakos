<script setup lang="ts">
const { locale, hydrateLocale } = useSiteLocale()
const { withBase } = useSiteAssetUrl()

const { data: navigation } = await useAsyncData('docs-navigation', () =>
  queryCollectionNavigation('docs')
)

const { data: files } = useLazyAsyncData(
  'docs-search-sections',
  () => queryCollectionSearchSections('docs'),
  { server: false }
)

provide('docs-navigation', navigation)

onMounted(() => {
  hydrateLocale()
})

useHead(() => ({
  htmlAttrs: {
    lang: locale.value === 'en' ? 'en' : 'zh-CN'
  },
  meta: [{ property: 'og:image', content: withBase('icon.png') }],
  link: [{ rel: 'icon', type: 'image/png', href: withBase('favicon.png') }]
}))
</script>

<template>
  <UApp>
    <div class="flex min-h-screen flex-col bg-default">
      <SiteHeader />

      <UMain class="flex-1">
        <NuxtLayout>
          <NuxtPage />
        </NuxtLayout>
      </UMain>

      <SiteFooter />
    </div>

    <ClientOnly>
      <LazyUContentSearch :files="files" :navigation="navigation" />
    </ClientOnly>
  </UApp>
</template>
