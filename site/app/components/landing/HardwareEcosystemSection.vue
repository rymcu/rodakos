<script setup lang="ts">
import { SITE_LINKS } from '~/config/links'

const { copy } = useLandingCopy()
const { withBase } = useSiteAssetUrl()

const enclosureImgSrc = computed(() => withBase('hardware/enclosure-exploded.webp'))
</script>

<template>
  <div class="border-y border-muted bg-elevated/25">
    <!-- 上半区：BigSmart-Open 硬件 + Rodak 本地工作台双生态卡 -->
    <UPageSection
      id="hardware"
      :headline="copy.hardware.headline"
      :title="copy.hardware.title"
      :description="copy.hardware.description"
      :ui="{
        container: 'pt-16 pb-10 sm:pt-24 sm:pb-12',
        title: 'text-2xl sm:text-4xl'
      }"
    >
      <div class="grid gap-5 lg:grid-cols-2">
        <UPageCard
          v-for="project in copy.hardware.projects"
          :key="project.id"
          :title="project.title"
          :description="project.description"
          :icon="project.icon"
          :to="project.to"
          target="_blank"
          variant="subtle"
          spotlight
          spotlight-color="primary"
          highlight
        >
          <template #header>
            <div class="flex flex-wrap items-center justify-between gap-2">
              <UBadge :label="project.badge" color="primary" variant="subtle" size="xs" />
              <span class="inline-flex items-center gap-1.5 font-mono text-xs text-muted">
                <UIcon name="i-simple-icons-github" class="size-3.5" />
                {{ project.repoLabel }}
              </span>
            </div>
          </template>

          <template #footer>
            <div class="w-full space-y-4">
              <div class="flex flex-wrap gap-1.5">
                <UBadge
                  v-for="spec in project.specs"
                  :key="spec"
                  :label="spec"
                  color="neutral"
                  variant="outline"
                  size="xs"
                  class="font-mono"
                />
              </div>
              <div class="inline-flex items-center gap-1.5 text-sm font-medium text-primary">
                <span>{{ project.ctaLabel }}</span>
                <UIcon name="i-lucide-arrow-up-right" class="size-4" />
              </div>
            </div>
          </template>
        </UPageCard>
      </div>
    </UPageSection>

    <!-- 下半区：全参数 3D 外壳 + 16 MB Flash Recovery 分区布局 -->
    <UPageSection
      orientation="horizontal"
      reverse
      :headline="copy.hardware.enclosureHeadline"
      :title="copy.hardware.enclosureTitle"
      :description="copy.hardware.enclosureDescription"
      :features="[...copy.hardware.enclosureFeatures]"
      :links="[
        {
          label: copy.hardware.enclosureCta,
          trailingIcon: 'i-lucide-arrow-right',
          to: SITE_LINKS.docsHardware,
          color: 'primary',
          variant: 'soft'
        },
        {
          label: 'BigSmart-Open / v6_parametric',
          icon: 'i-simple-icons-github',
          to: SITE_LINKS.enclosure,
          target: '_blank',
          color: 'neutral',
          variant: 'outline'
        }
      ]"
      :ui="{
        container: 'pt-8 pb-16 sm:pt-12 sm:pb-24',
        title: 'text-2xl sm:text-3xl'
      }"
    >
      <div class="space-y-4">
        <!-- 16 MB Flash 分区表可视化卡片 -->
        <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'space-y-4 p-5 sm:p-6' }">
          <div class="flex flex-wrap items-center justify-between gap-2">
            <h3 class="text-sm font-semibold text-highlighted">
              {{ copy.hardware.partitionTitle }}
            </h3>
            <UBadge
              :label="copy.hardware.partitionBadge"
              color="primary"
              variant="subtle"
              size="xs"
              class="font-mono"
            />
          </div>

          <div class="flex h-7 w-full overflow-hidden rounded-lg border border-default bg-default">
            <div
              v-for="seg in copy.hardware.partitions"
              :key="seg.name"
              class="flex items-center justify-center overflow-hidden px-1 font-mono text-[10px] font-semibold text-white"
              :class="seg.colorClass"
              :style="{ width: `${seg.widthPercent}%` }"
              :title="`${seg.name} (${seg.sizeLabel})`"
            >
              <span class="truncate">{{ seg.name }}</span>
            </div>
          </div>

          <div class="grid gap-2 sm:grid-cols-2">
            <div
              v-for="seg in copy.hardware.partitions"
              :key="seg.name"
              class="rounded-lg border border-default bg-default/75 p-2.5"
            >
              <div class="flex items-center justify-between gap-2">
                <span class="inline-flex items-center gap-1.5 font-mono text-xs font-semibold text-highlighted">
                  <span class="size-2.5 rounded-xs" :class="seg.colorClass" />
                  {{ seg.name }}
                </span>
                <span class="font-mono text-[11px] text-primary">{{ seg.sizeLabel }}</span>
              </div>
              <p class="mt-1 text-[11px] leading-relaxed text-muted">{{ seg.role }}</p>
            </div>
          </div>

          <p class="text-[11px] leading-relaxed text-dimmed">
            {{ copy.hardware.partitionSummary }}
          </p>
        </UCard>

        <!-- 3D 参数化外壳交互式真实 CAD 模型查看器（来自 3d/enclosure/out/step/*.step） -->
        <ClientOnly>
          <LandingEnclosure3DViewer />
          <template #fallback>
            <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default overflow-hidden', body: 'p-3' }">
              <div class="overflow-hidden rounded-lg border border-default bg-white">
                <img
                  :src="enclosureImgSrc"
                  :alt="copy.hardware.enclosureImageAlt"
                  width="1400"
                  height="650"
                  loading="lazy"
                  decoding="async"
                  class="h-auto w-full object-contain"
                >
              </div>
              <p class="mt-2 px-1 text-[11px] text-muted">
                {{ copy.hardware.enclosureImageCaption }}
              </p>
            </UCard>
          </template>
        </ClientOnly>
      </div>
    </UPageSection>
  </div>
</template>
