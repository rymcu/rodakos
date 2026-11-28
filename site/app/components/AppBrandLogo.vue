<script setup lang="ts">
const { compact = false } = defineProps<{
  compact?: boolean
}>()
</script>

<template>
  <span class="inline-flex items-center gap-2 select-none">
    <span class="font-edix text-highlighted text-lg tracking-wider">RODAKOS</span>
    <UBadge
      v-if="!compact"
      label="v0.1.6"
      color="primary"
      variant="subtle"
      size="xs"
      class="font-mono"
    />
  </span>
</template>
