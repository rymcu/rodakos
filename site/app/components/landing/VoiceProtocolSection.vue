<script setup lang="ts">
import type { TabsItem } from '@nuxt/ui'
import { SITE_LINKS } from '~/config/links'
import type { VoiceStageId } from '~/types/landing'

const { copy } = useLandingCopy()

const selectedStage = shallowRef<VoiceStageId>('wake')

const stageTabs = computed<TabsItem[]>(() =>
  copy.value.voice.stages.map((stage) => ({
    label: `${stage.step} ${stage.title}`,
    icon: stage.icon,
    value: stage.id
  }))
)

const activeStage = computed(() => {
  return (
    copy.value.voice.stages.find((stage) => stage.id === selectedStage.value) ??
    copy.value.voice.stages[0]!
  )
})
</script>

<template>
  <UPageSection
    id="voice"
    :headline="copy.voice.headline"
    :title="copy.voice.title"
    :description="copy.voice.description"
    :ui="{
      container: 'py-16 sm:py-24',
      title: 'text-2xl sm:text-4xl'
    }"
  >
    <div class="space-y-6">
      <div class="flex flex-wrap items-center justify-between gap-3">
        <div class="max-w-full overflow-x-auto pb-1 sm:pb-0">
          <UTabs
            v-model="selectedStage"
            :items="stageTabs"
            :content="false"
            color="primary"
            variant="pill"
            size="sm"
            :ui="{
              root: 'w-max shrink-0',
              list: 'w-max flex-nowrap',
              trigger: 'shrink-0 whitespace-nowrap'
            }"
          />
        </div>
        <UBadge
          :label="copy.voice.diagramBadge"
          icon="i-lucide-git-commit-horizontal"
          color="neutral"
          variant="subtle"
          size="xs"
          class="shrink-0"
        />
      </div>

      <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'p-5 sm:p-6' }">
        <div class="grid gap-6 lg:grid-cols-12 lg:items-center">
          <div class="space-y-2.5 lg:col-span-6">
            <div class="flex flex-wrap items-center gap-2">
              <UBadge
                :label="`STAGE ${activeStage.step}`"
                color="primary"
                variant="subtle"
                size="xs"
                class="font-mono"
              />
              <UBadge
                :label="activeStage.engine"
                color="neutral"
                variant="outline"
                size="xs"
                class="font-mono"
              />
              <UBadge
                :label="activeStage.metric"
                color="success"
                variant="subtle"
                size="xs"
                class="font-mono"
              />
            </div>

            <h3 class="text-xl font-semibold text-highlighted">
              {{ activeStage.title }}
            </h3>
            <p class="text-sm leading-relaxed text-muted">
              {{ activeStage.summary }}
            </p>
          </div>

          <div class="space-y-2.5 lg:col-span-6">
            <div
              v-for="point in activeStage.highlights"
              :key="point"
              class="flex items-start gap-2.5 rounded-lg border border-default bg-default/75 px-3.5 py-2.5 text-xs text-default"
            >
              <UIcon name="i-lucide-check" class="mt-0.5 size-4 shrink-0 text-primary" />
              <span>{{ point }}</span>
            </div>
          </div>
        </div>

        <template #footer>
          <div class="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
            <p class="text-xs text-dimmed">
              {{ copy.voice.diagramNote }}
            </p>
            <UButton
              :label="copy.voice.labCta"
              icon="i-lucide-flask-conical"
              trailing-icon="i-lucide-arrow-right"
              :to="SITE_LINKS.protocolsLabPath"
              color="primary"
              variant="soft"
              size="xs"
              class="shrink-0 self-start sm:self-auto"
            />
          </div>
        </template>
      </UCard>

      <UPageGrid>
        <UPageCard
          v-for="card in copy.voice.cards"
          :key="card.title"
          :title="card.title"
          :description="card.description"
          :icon="card.icon"
          variant="outline"
          spotlight
          spotlight-color="primary"
        />
      </UPageGrid>
    </div>
  </UPageSection>
</template>
