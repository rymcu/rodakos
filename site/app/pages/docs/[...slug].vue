<script setup lang="ts">
definePageMeta({
  layout: 'docs'
})

const route = useRoute()

const { data: page } = await useAsyncData(`docs-page-${route.path}`, () =>
  queryCollection('docs').path(route.path).first()
)

if (!page.value) {
  throw createError({
    statusCode: 404,
    statusMessage: '未找到对应文档页面'
  })
}

const { data: surround } = await useAsyncData(`docs-surround-${route.path}`, () =>
  queryCollectionItemSurroundings('docs', route.path, {
    fields: ['description']
  })
)

const headerLinks = computed(() =>
  (page.value?.contracts ?? []).map((item) => ({
    label: item.label,
    to: item.to,
    target: '_blank',
    icon: 'i-simple-icons-github',
    color: 'neutral' as const,
    variant: 'subtle' as const,
    size: 'xs' as const
  }))
)

const chapterLabel = computed(() => {
  const ch = page.value?.chapter
  if (!ch) return ''
  return ch === '00' ? '总索引 · 路线图' : `第 ${ch} 篇 / 13`
})

useSeoMeta({
  title: () => `${page.value?.title ?? '开发文档'} · RodakOS 文档`,
  description: () => page.value?.description ?? ''
})
</script>

<template>
  <UPage v-if="page">
    <UPageHeader
      :title="page.title"
      :description="page.description"
      :headline="page.category"
      :links="headerLinks"
    >
      <template v-if="chapterLabel || page.baseline || page.part" #default>
        <div class="mt-4 flex flex-wrap items-center gap-2">
          <UBadge
            v-if="chapterLabel"
            :label="chapterLabel"
            color="primary"
            variant="subtle"
            size="sm"
            class="font-mono"
          />
          <UBadge
            v-if="page.part"
            :label="page.part"
            color="neutral"
            variant="outline"
            size="sm"
          />
          <UBadge
            v-if="page.baseline"
            :label="`基线 ${page.baseline}`"
            icon="i-lucide-git-commit-horizontal"
            color="neutral"
            variant="subtle"
            size="sm"
            class="font-mono"
          />
        </div>
      </template>
    </UPageHeader>

    <UPageBody>
      <ContentRenderer :value="page" />

      <USeparator class="my-8" />

      <UContentSurround :surround="surround" />
    </UPageBody>

    <template v-if="page.body?.toc?.links?.length" #right>
      <UContentToc :links="page.body.toc.links" title="本页目录" />
    </template>
  </UPage>
</template>
