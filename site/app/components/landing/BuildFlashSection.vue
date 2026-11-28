<script setup lang="ts">
import type { TabsItem } from '@nuxt/ui'
import { SITE_LINKS } from '~/config/links'
import type { BuildTabId } from '~/types/landing'

const { copy } = useLandingCopy()
const { copiedKey, copyText } = useClipboardCopy()

const activeCommandTab = shallowRef<BuildTabId>('firmware')

const commandTabs = computed<TabsItem[]>(() => [
  {
    label: copy.value.buildFlash.commandTabs.firmwareLabel,
    icon: 'i-lucide-terminal',
    value: 'firmware'
  },
  {
    label: copy.value.buildFlash.commandTabs.hostTestsLabel,
    icon: 'i-lucide-flask-conical',
    value: 'host-tests'
  }
])

const activeCommandTitle = computed(() =>
  activeCommandTab.value === 'firmware'
    ? copy.value.buildFlash.commandTabs.firmwareTitle
    : copy.value.buildFlash.commandTabs.hostTestsTitle
)

const activeCommandCode = computed(() =>
  activeCommandTab.value === 'firmware'
    ? copy.value.buildFlash.commandTabs.firmwareCode
    : copy.value.buildFlash.commandTabs.hostTestsCode
)

const activeCommandNote = computed(() =>
  activeCommandTab.value === 'firmware'
    ? copy.value.buildFlash.commandTabs.firmwareNote
    : copy.value.buildFlash.commandTabs.hostTestsNote
)
</script>

<template>
  <UPageSection
    id="build-flash"
    :headline="copy.buildFlash.headline"
    :title="copy.buildFlash.title"
    :description="copy.buildFlash.description"
    :ui="{
      container: 'py-16 sm:py-24',
      title: 'text-2xl sm:text-4xl'
    }"
  >
    <div class="space-y-6">
      <div class="grid gap-5 lg:grid-cols-12 lg:items-start">
        <!-- 左卡：可视化快速刷机与配网 -->
        <UPageCard
          :title="copy.buildFlash.quickstart.title"
          :description="copy.buildFlash.quickstart.description"
          :icon="copy.buildFlash.quickstart.icon"
          variant="subtle"
          spotlight
          spotlight-color="primary"
          class="min-w-0 lg:col-span-5"
        >
          <template #header>
            <UBadge
              :label="copy.buildFlash.quickstart.badge"
              color="neutral"
              variant="subtle"
              size="xs"
            />
          </template>

          <ul class="mt-2 space-y-2.5">
            <li
              v-for="item in copy.buildFlash.quickstart.highlights"
              :key="item"
              class="flex items-start gap-2.5 text-xs leading-relaxed text-default"
            >
              <UIcon name="i-lucide-check-circle-2" class="mt-0.5 size-4 shrink-0 text-primary" />
              <span>{{ item }}</span>
            </li>
          </ul>

          <template #footer>
            <div class="flex flex-wrap gap-2.5">
              <UButton
                :label="copy.buildFlash.quickstart.primaryLabel"
                icon="i-lucide-download"
                :to="copy.buildFlash.quickstart.primaryTo"
                :target="copy.buildFlash.quickstart.primaryExternal ? '_blank' : undefined"
                color="primary"
                variant="solid"
                size="md"
              />
              <UButton
                :label="copy.buildFlash.quickstart.secondaryLabel"
                :to="copy.buildFlash.quickstart.secondaryTo"
                color="neutral"
                variant="outline"
                size="md"
              />
            </div>
          </template>
        </UPageCard>

        <!-- 右卡：ESP-IDF 6.0.2 源码构建与主机端回归测试 -->
        <UPageCard
          :title="copy.buildFlash.developer.title"
          :description="copy.buildFlash.developer.description"
          :icon="copy.buildFlash.developer.icon"
          variant="subtle"
          spotlight
          spotlight-color="primary"
          highlight
          :ui="{ container: 'min-w-0', wrapper: 'min-w-0', body: 'min-w-0 w-full' }"
          class="min-w-0 lg:col-span-7"
        >
          <template #header>
            <UBadge
              :label="copy.buildFlash.developer.badge"
              color="primary"
              variant="subtle"
              size="xs"
            />
          </template>

          <div class="mt-2 min-w-0 space-y-4">
            <UTabs
              v-model="activeCommandTab"
              :items="commandTabs"
              :content="false"
              size="xs"
              color="primary"
              variant="pill"
            />

            <div class="max-w-full overflow-hidden rounded-xl border border-default bg-default">
              <div
                class="flex items-center justify-between border-b border-muted bg-elevated/70 px-3.5 py-2"
              >
                <span class="inline-flex items-center gap-2 font-mono text-xs text-muted">
                  <UIcon name="i-lucide-terminal" class="size-3.5 text-primary" />
                  {{ activeCommandTitle }}
                </span>
                <UButton
                  :label="
                    copiedKey === activeCommandTab ? copy.buildFlash.copiedLabel : copy.buildFlash.copyLabel
                  "
                  :icon="copiedKey === activeCommandTab ? 'i-lucide-check' : 'i-lucide-copy'"
                  color="neutral"
                  variant="ghost"
                  size="xs"
                  @click="copyText(activeCommandTab, activeCommandCode)"
                />
              </div>
              <pre
                class="overflow-x-auto p-3.5 font-mono text-xs leading-relaxed text-default"
              ><code>{{ activeCommandCode }}</code></pre>
            </div>
            <p class="text-xs text-muted">{{ activeCommandNote }}</p>

            <ul class="grid gap-2 sm:grid-cols-2">
              <li
                v-for="item in copy.buildFlash.developer.highlights"
                :key="item"
                class="flex items-start gap-2 text-xs leading-relaxed text-default"
              >
                <UIcon name="i-lucide-check" class="mt-0.5 size-3.5 shrink-0 text-primary" />
                <span>{{ item }}</span>
              </li>
            </ul>
          </div>

          <template #footer>
            <div class="flex flex-wrap gap-2.5">
              <UButton
                :label="copy.buildFlash.developer.primaryLabel"
                icon="i-lucide-book-open"
                :to="copy.buildFlash.developer.primaryTo"
                color="primary"
                variant="soft"
                size="md"
              />
              <UButton
                :label="copy.buildFlash.developer.secondaryLabel"
                icon="i-lucide-external-link"
                :to="copy.buildFlash.developer.secondaryTo"
                :target="copy.buildFlash.developer.secondaryExternal ? '_blank' : undefined"
                color="neutral"
                variant="ghost"
                size="md"
              />
            </div>
          </template>
        </UPageCard>
      </div>

      <!-- 发布门禁透明公示（NO_GO） -->
      <UCard variant="subtle" :ui="{ root: 'ring-1 ring-warning/40 bg-warning/5', body: 'space-y-3 p-5' }">
        <div class="flex flex-wrap items-center justify-between gap-2">
          <div class="flex flex-wrap items-center gap-2">
            <UIcon name="i-lucide-clipboard-check" class="size-4.5 text-warning" />
            <h3 class="text-sm font-semibold text-highlighted">
              {{ copy.buildFlash.readinessTitle }}
            </h3>
            <UBadge
              :label="copy.buildFlash.readinessBadge"
              color="warning"
              variant="subtle"
              size="xs"
              class="font-mono"
            />
          </div>
          <UButton
            :label="copy.buildFlash.readinessCta"
            trailing-icon="i-lucide-arrow-up-right"
            :to="SITE_LINKS.readinessDoc"
            target="_blank"
            color="warning"
            variant="soft"
            size="xs"
          />
        </div>

        <p class="text-xs leading-relaxed text-muted">
          {{ copy.buildFlash.readinessDesc }}
        </p>

        <div class="grid gap-2 sm:grid-cols-3">
          <a
            v-for="gate in copy.buildFlash.readinessGates"
            :key="gate.issue"
            :href="gate.url"
            target="_blank"
            rel="noopener noreferrer"
            class="flex items-start justify-between gap-2 rounded-lg border border-default bg-default/80 px-3 py-2 text-xs transition-colors hover:border-warning"
          >
            <div class="min-w-0">
              <span class="font-mono font-semibold text-warning">{{ gate.issue }}</span>
              <p class="mt-0.5 text-default">{{ gate.label }}</p>
            </div>
            <UIcon name="i-lucide-arrow-up-right" class="mt-0.5 size-3.5 shrink-0 text-muted" />
          </a>
        </div>
      </UCard>
    </div>
  </UPageSection>
</template>
