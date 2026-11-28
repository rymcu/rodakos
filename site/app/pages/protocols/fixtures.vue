<script setup lang="ts">
definePageMeta({
  layout: 'protocols'
})

const route = useRoute()
const { items, loaded, loadError, t, ensureLoaded } = useProtocolFixtures()
const { copiedKey, copyText } = useClipboardCopy()

useSeoMeta({
  title: () => `${t('契约示例 Fixtures · 协议实验室', 'Contract Fixtures · Protocol Lab')} · RodakOS`,
  description: () =>
    t(
      '浏览并检索 contracts/ 下自动生成的全部正反例 JSON 与 RAV1 测试夹具。',
      'Browse and filter all auto-generated valid and invalid JSON & RAV1 fixtures from contracts/.'
    )
})

const selectedContract = shallowRef('__all__')
const selectedSchema = shallowRef('__all__')
const selectedKind = shallowRef('__all__')
const searchQuery = shallowRef(typeof route.query.q === 'string' ? route.query.q : '')

const contractOptions = computed(() => [
  { value: '__all__', label: t('全部契约', 'All contracts') },
  ...[...new Set(items.value.map((x) => x.contract))].map((c) => ({ value: c, label: c }))
])

const schemaOptions = computed(() => {
  const names = [
    ...new Set(
      items.value
        .filter((x) => selectedContract.value === '__all__' || x.contract === selectedContract.value)
        .map((x) => x.schema || '(none)')
    )
  ].sort()
  return [
    { value: '__all__', label: t('全部 Schema', 'All schemas') },
    ...names.map((n) => ({ value: n, label: n.replace('.schema.json', '') }))
  ]
})

const kindOptions = computed(() => [
  { value: '__all__', label: t('全部类型', 'All kinds') },
  { value: 'valid', label: 'valid' },
  { value: 'invalid', label: 'invalid' }
])

watch(selectedContract, () => {
  selectedSchema.value = '__all__'
})

const filtered = computed(() => {
  const q = searchQuery.value.trim().toLowerCase()
  return items.value.filter((x) => {
    if (selectedContract.value !== '__all__' && x.contract !== selectedContract.value) return false
    if (selectedSchema.value !== '__all__' && (x.schema || '(none)') !== selectedSchema.value) {
      return false
    }
    if (selectedKind.value !== '__all__' && x.kind !== selectedKind.value) return false
    if (q && !x.filename.toLowerCase().includes(q)) return false
    return true
  })
})

function formatBody(content: unknown): string {
  return typeof content === 'string' ? content : JSON.stringify(content, null, 2)
}

onMounted(() => {
  void ensureLoaded()
})
</script>

<template>
  <div class="space-y-6">
    <UPageHeader
      headline="contracts/*/v1/examples"
      :title="t('契约示例浏览器（Fixtures）', 'Contract Fixtures Browser')"
      :description="
        t(
          '契约仓库里每个 Schema 必须接受（valid）或拒绝（invalid）的真实报文样例。反例文件名直接标明所违反的规则；数据由构建脚本从 contracts/ 自动同步。',
          'Real valid and invalid fixtures each contract schema must accept or reject. Invalid filenames name the rule broken; generated directly from contracts/.'
        )
      "
    />

    <!-- 过滤工具栏 -->
    <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'space-y-4 p-4 sm:p-5' }">
      <div class="grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
        <div class="space-y-1">
          <label class="block text-xs font-medium text-muted">{{ t('契约', 'Contract') }}</label>
          <USelect
            v-model="selectedContract"
            :items="contractOptions"
            value-key="value"
            label-key="label"
            class="w-full"
          />
        </div>
        <div class="space-y-1">
          <label class="block text-xs font-medium text-muted">{{ t('Schema（结构定义）', 'Schema') }}</label>
          <USelect
            v-model="selectedSchema"
            :items="schemaOptions"
            value-key="value"
            label-key="label"
            class="w-full"
          />
        </div>
        <div class="space-y-1">
          <label class="block text-xs font-medium text-muted">{{ t('类型', 'Kind') }}</label>
          <USelect
            v-model="selectedKind"
            :items="kindOptions"
            value-key="value"
            label-key="label"
            class="w-full"
          />
        </div>
        <div class="space-y-1">
          <label class="block text-xs font-medium text-muted" for="fx-search">
            {{ t('文件名过滤', 'Filter by filename') }}
          </label>
          <UInput
            id="fx-search"
            v-model="searchQuery"
            icon="i-lucide-search"
            :placeholder="t('例如 inbox / caption / announce', 'e.g. inbox / caption / announce')"
            class="w-full"
          />
        </div>
      </div>

      <div class="flex flex-wrap items-center justify-between gap-2 text-xs text-muted">
        <span>
          {{
            loaded
              ? t(`显示 ${filtered.length} / ${items.length} 条夹具`, `Showing ${filtered.length} of ${items.length} fixtures`)
              : t('正在加载契约夹具...', 'Loading contract fixtures...')
          }}
        </span>
        <UButton
          v-if="
            selectedContract !== '__all__' ||
            selectedSchema !== '__all__' ||
            selectedKind !== '__all__' ||
            searchQuery
          "
          :label="t('清空筛选', 'Reset filters')"
          color="neutral"
          variant="ghost"
          size="xs"
          @click="
            selectedContract = '__all__';
            selectedSchema = '__all__';
            selectedKind = '__all__';
            searchQuery = ''
          "
        />
      </div>
    </UCard>

    <UAlert v-if="loadError" color="error" variant="subtle" :description="loadError" />

    <!-- 夹具卡片网格 -->
    <div class="grid gap-4 md:grid-cols-2">
      <UCard
        v-for="item in filtered"
        :key="`${item.contract}/${item.kind}/${item.filename}`"
        variant="subtle"
        :ui="{ root: 'ring-1 ring-default', body: 'space-y-3 p-4' }"
      >
        <div class="flex items-start justify-between gap-2">
          <code class="break-all font-mono text-xs font-semibold text-highlighted">
            {{ item.filename }}
          </code>
          <UButton
            :icon="copiedKey === item.filename ? 'i-lucide-check' : 'i-lucide-copy'"
            color="neutral"
            variant="ghost"
            size="xs"
            aria-label="复制夹具内容"
            @click="copyText(item.filename, formatBody(item.content))"
          />
        </div>

        <div class="flex flex-wrap items-center gap-1.5">
          <UBadge
            :label="item.kind"
            :color="item.kind === 'valid' ? 'success' : 'error'"
            variant="subtle"
            size="xs"
            class="font-mono"
          />
          <UBadge :label="item.contract" color="neutral" variant="outline" size="xs" class="font-mono" />
          <span class="font-mono text-[11px] text-muted">
            {{ item.schema || t('无对应 schema', 'no schema') }}
          </span>
        </div>

        <details class="group">
          <summary class="cursor-pointer text-xs font-medium text-primary select-none">
            {{ t('展开报文内容', 'Show payload') }}
          </summary>
          <pre
            class="mt-2 max-h-64 overflow-auto rounded-lg border border-default bg-default/80 p-3 font-mono text-[11px] leading-relaxed text-toned"
          >{{ formatBody(item.content) }}</pre>
        </details>
      </UCard>
    </div>
  </div>
</template>
