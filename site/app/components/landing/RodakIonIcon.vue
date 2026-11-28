<script setup lang="ts">
import { RODAKOS_ION_ICONS, type RodakosIonName } from '~/data/rodakos-icons'

const props = withDefaults(
  defineProps<{
    name: RodakosIonName
    size?: number
    color?: string
  }>(),
  {
    size: 18,
    color: 'currentColor'
  }
)

const svgBody = computed(() => RODAKOS_ION_ICONS[props.name] ?? '')
</script>

<template>
  <svg
    :width="size"
    :height="size"
    viewBox="0 0 512 512"
    :style="{ color, display: 'block', flexShrink: 0 }"
    xmlns="http://www.w3.org/2000/svg"
    aria-hidden="true"
    v-html="svgBody"
  />
</template>
