<script setup lang="ts">
const props = defineProps<{
  href?: string
  target?: string
}>()

const isExternalHttp = computed(() => Boolean(props.href && /^https?:\/\//i.test(props.href)))

const resolvedTarget = computed(() => {
  if (props.target) return props.target
  if (isExternalHttp.value) return '_blank'
  return undefined
})
</script>

<template>
  <ULink
    :to="props.href"
    :target="resolvedTarget"
    :rel="isExternalHttp ? 'noopener noreferrer' : undefined"
    class="font-medium text-primary underline decoration-primary/40 underline-offset-4 transition-colors hover:decoration-primary"
    raw
  >
    <slot />
  </ULink>
</template>
