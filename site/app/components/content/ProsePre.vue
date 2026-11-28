<script setup lang="ts">
const props = withDefaults(
  defineProps<{
    icon?: string
    code?: string
    language?: string
    filename?: string
    highlights?: number[]
    hideHeader?: boolean
    meta?: string
    copy?: boolean
    class?: string
  }>(),
  {
    copy: true
  }
)

const copyKey = useId()
const { copiedKey, copyText } = useClipboardCopy()
const copied = computed(() => copiedKey.value === copyKey)
const preRef = useTemplateRef<HTMLPreElement>('preRef')

const isMermaid = computed(() => props.language?.toLowerCase() === 'mermaid')

function handleCopy() {
  const text = props.code ?? preRef.value?.textContent ?? ''
  if (text) {
    void copyText(copyKey, text)
  }
}
</script>

<template>
  <MermaidDiagram v-if="isMermaid" :code="props.code ?? ''" :title="props.filename" />

  <div
    v-else
    class="group relative my-5 overflow-hidden rounded-xl border border-default bg-elevated/45"
  >
    <div
      v-if="props.filename && !props.hideHeader"
      class="flex items-center gap-2 border-b border-default bg-elevated/70 px-3.5 py-2 text-xs text-muted"
    >
      <UIcon
        :name="props.icon || 'i-lucide-file-code-2'"
        class="size-3.5 shrink-0 text-primary"
      />
      <span class="font-mono font-medium text-highlighted">{{ props.filename }}</span>
      <span v-if="props.language" class="ml-auto font-mono text-[11px] uppercase text-dimmed">
        {{ props.language }}
      </span>
    </div>

    <UButton
      v-if="props.copy"
      :icon="copied ? 'i-lucide-check' : 'i-lucide-copy'"
      color="neutral"
      variant="outline"
      size="xs"
      aria-label="复制代码"
      class="absolute right-2.5 z-10 opacity-75 transition-opacity hover:opacity-100"
      :class="props.filename && !props.hideHeader ? 'top-1.5' : 'top-2.5'"
      @click="handleCopy"
    />

    <pre
      ref="preRef"
      class="m-0 overflow-x-auto px-4 py-3.5 font-mono text-[13px] leading-relaxed"
      :class="props.class"
      v-bind="$attrs"
    ><slot /></pre>
  </div>
</template>
