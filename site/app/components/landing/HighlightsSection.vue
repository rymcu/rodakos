<script setup lang="ts">
const { copy } = useLandingCopy()
</script>

<template>
  <UPageSection
    id="highlights"
    :headline="copy.highlights.headline"
    :title="copy.highlights.title"
    :description="copy.highlights.description"
    :ui="{
      container: 'py-16 sm:py-24',
      title: 'text-2xl sm:text-4xl'
    }"
  >
    <UPageGrid>
      <UPageCard
        v-for="item in copy.highlights.items"
        :key="item.id"
        :title="item.title"
        :description="item.description"
        :icon="item.icon"
        variant="subtle"
        spotlight
        spotlight-color="primary"
      >
        <template #footer>
          <UBadge :label="item.badge" color="primary" variant="subtle" size="xs" />
        </template>
      </UPageCard>
    </UPageGrid>
  </UPageSection>
</template>
