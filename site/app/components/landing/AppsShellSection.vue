<script setup lang="ts">
import type { TabsItem } from '@nuxt/ui'
import type { AppCategoryId, ScreenGalleryItem, ScreenSceneId } from '~/types/landing'

const { copy } = useLandingCopy()

const activeCategory = shallowRef<AppCategoryId>('system')

const categoryTabs = computed<TabsItem[]>(() =>
  copy.value.appsShell.categories.map((group) => ({
    label: `${group.label} (${group.apps.length})`,
    icon: group.icon,
    value: group.id
  }))
)

const currentGroup = computed(
  () =>
    copy.value.appsShell.categories.find((g) => g.id === activeCategory.value) ??
    copy.value.appsShell.categories[0]!
)

const galleryItems = computed<ScreenGalleryItem[]>(() =>
  copy.value.appsShell.categories.map((group) => ({
    id: group.sceneId,
    label: group.label,
    icon: group.icon,
    title: group.screenTitle,
    alt: group.screenAlt,
    caption: group.screenCaption,
    darkSrc: group.darkSrc,
    lightSrc: group.lightSrc
  }))
)

function handleSyncScene(sceneId: ScreenSceneId): void {
  const matched = copy.value.appsShell.categories.find((g) => g.sceneId === sceneId)
  if (matched) {
    activeCategory.value = matched.id
  }
}
</script>

<template>
  <div class="border-y border-muted bg-elevated/25">
    <UPageSection
      id="apps-shell"
      :headline="copy.appsShell.headline"
      :title="copy.appsShell.title"
      :description="copy.appsShell.description"
      :ui="{
        container: 'py-16 sm:py-24',
        title: 'text-2xl sm:text-4xl'
      }"
    >
      <div class="grid gap-8 lg:grid-cols-12 lg:items-start">
        <div class="min-w-0 space-y-5 lg:col-span-5">
          <div class="space-y-3.5">
            <div
              v-for="feature in copy.appsShell.features"
              :key="feature.title"
              class="flex items-start gap-3 rounded-xl border border-default bg-default/75 p-4"
            >
              <div
                class="flex size-9 shrink-0 items-center justify-center rounded-lg bg-primary/10 text-primary"
              >
                <UIcon :name="feature.icon" class="size-4.5" />
              </div>
              <div class="min-w-0">
                <h3 class="text-sm font-semibold text-highlighted">{{ feature.title }}</h3>
                <p class="mt-1 text-xs leading-relaxed text-muted">{{ feature.description }}</p>
              </div>
            </div>
          </div>

          <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'space-y-3.5 p-4 sm:p-5' }">
            <div class="flex items-center justify-between gap-2">
              <p class="text-xs font-semibold text-highlighted">
                {{ currentGroup.label }}
              </p>
              <UBadge
                :label="`${currentGroup.apps.length} / 16`"
                color="primary"
                variant="subtle"
                size="xs"
                class="font-mono"
              />
            </div>

            <div class="space-y-2">
              <div
                v-for="app in currentGroup.apps"
                :key="app.id"
                class="flex items-start gap-2.5 rounded-lg border border-default bg-default/80 px-3 py-2"
              >
                <span
                  class="mt-0.5 flex size-6 shrink-0 items-center justify-center rounded-md text-white shadow-xs"
                  :style="{ backgroundColor: app.slotColor }"
                >
                  <UIcon :name="app.icon" class="size-3.5" />
                </span>
                <div class="min-w-0 flex-1">
                  <div class="flex flex-wrap items-center justify-between gap-1.5">
                    <span class="text-xs font-semibold text-highlighted">{{ app.name }}</span>
                    <UBadge
                      :label="app.offline ? copy.appsShell.offlineBadge : copy.appsShell.cloudBadge"
                      :color="app.offline ? 'success' : 'primary'"
                      variant="subtle"
                      size="xs"
                    />
                  </div>
                  <p class="mt-0.5 text-[11px] leading-relaxed text-muted">{{ app.summary }}</p>
                </div>
              </div>
            </div>

            <UAlert
              :description="currentGroup.note"
              icon="i-lucide-layout-grid"
              color="primary"
              variant="subtle"
            />
          </UCard>
        </div>

        <div class="min-w-0 space-y-4 lg:col-span-7">
          <div class="flex max-w-full justify-start overflow-x-auto pb-1 lg:justify-start lg:pb-0">
            <UTabs
              v-model="activeCategory"
              :items="categoryTabs"
              :content="false"
              size="sm"
              color="primary"
              variant="pill"
              :ui="{
                root: 'w-max shrink-0',
                list: 'w-max flex-nowrap',
                trigger: 'shrink-0 whitespace-nowrap'
              }"
            />
          </div>

          <LandingRealScreenFrame
            :items="galleryItems"
            :active-id="currentGroup.sceneId"
            compact
            @update:active-id="handleSyncScene"
          />
        </div>
      </div>
    </UPageSection>
  </div>
</template>
