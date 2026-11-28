<script setup lang="ts">
import type { FirmwareThemeId, ScreenGalleryItem, ScreenSceneId } from '~/types/landing'

const props = withDefaults(
  defineProps<{
    items: readonly ScreenGalleryItem[]
    activeId: ScreenSceneId
    eager?: boolean
    compact?: boolean
  }>(),
  {
    eager: false,
    compact: false
  }
)

const emit = defineEmits<{
  'update:activeId': [id: ScreenSceneId]
}>()

const { copy, locale } = useLandingCopy()
const { firmwareTheme, boardLedColor, boardLedIntensity } = useRodakDemoOs()

const isEn = computed(() => locale.value === 'en')

const currentScene = shallowRef<ScreenSceneId>(props.activeId)
const modalOpen = shallowRef(false)
const activeSwitch = shallowRef<'power' | 'boot' | 'io10' | null>(null)

interface ScreenExposeApi {
  goHome: (fromButton?: boolean) => void
  toggleControlCenter: (fromButton?: boolean) => void
  launchSmartFromButton: () => void
  lockDevice: (fromButton?: boolean) => void
  togglePowerSleep: () => void
}

const screenRef = useTemplateRef<ScreenExposeApi>('screenRef')
const modalScreenRef = useTemplateRef<ScreenExposeApi>('modalScreenRef')

watch(
  () => props.activeId,
  (next) => {
    currentScene.value = next
  }
)

const firmwareThemeOptions: readonly { id: FirmwareThemeId; dotClass: string }[] = [
  { id: 'dark', dotClass: 'bg-slate-950 ring-1 ring-slate-500' },
  { id: 'light', dotClass: 'bg-white ring-1 ring-slate-400' },
  { id: 'blue', dotClass: 'bg-[#1976D2] ring-1 ring-sky-300' },
  { id: 'green', dotClass: 'bg-[#2E7D32] ring-1 ring-emerald-300' }
]

const SCENE_LABELS_ZH: Record<ScreenSceneId, string> = {
  home: 'Home 桌面 · 第 1 页（支持左右滑动翻页）',
  'home-page2': 'Home 桌面 · 第 2 页',
  'voice-caption': 'Shell 全局底部语音字幕',
  calendar: 'Calendar 离线农历月历（点击日期进日视图）',
  gyro: 'Gyro 陀螺仪气泡水平仪',
  smart: 'Smart 智能灯控（同步 LED1）',
  themes: 'Themes 主题预设',
  'themes-trust': 'Themes 发布端物理触摸信任',
  assistant: 'Assistant 语音助手',
  recorder: 'Recorder 20 柱实时录音',
  wol: 'Wake 局域网唤醒',
  clock: 'Clock 本地时钟与 SNTP',
  settings: 'Settings 系统设置（含按键与壳层设置）',
  system: '关于本机 · 硬件与内存状态',
  music: 'Music 本地音频播放',
  files: 'Files SD 卡文件管理',
  photos: 'Photos 4×2 网格相册',
  camera: 'Camera GC0308 取景与右侧快门',
  messages: 'Messages 消息与待办中心'
}

const SCENE_LABELS_EN: Record<ScreenSceneId, string> = {
  home: 'Home Launcher · Page 1 (Swipe horizontally)',
  'home-page2': 'Home Launcher · Page 2',
  'voice-caption': 'Shell Global Voice Caption Bar',
  calendar: 'Calendar · Tap any date for Day View',
  gyro: 'Gyro · Interactive Bubble Level',
  smart: 'Smart Lights (Syncs Front LED1)',
  themes: 'Themes · Built-in Presets',
  'themes-trust': 'Themes · Physical Touch Trust',
  assistant: 'Assistant · Local Wake & Dialog',
  recorder: 'Recorder · 20-Bar Live Meter',
  wol: 'Wake-on-LAN · UDP Magic Packet',
  clock: 'Clock · Live Time & SNTP',
  settings: 'Settings · Buttons & Shell Preferences',
  system: 'About Device · Firmware & Memory',
  music: 'Music · Local PCM / MP3 Player',
  files: 'Files · Protected SD Browser',
  photos: 'Photos · 4×2 Grid Gallery',
  camera: 'Camera · GC0308 Viewfinder & Side Rail',
  messages: 'Messages · Todos & Alerts'
}

const matchedGalleryItem = computed<ScreenGalleryItem | null>(() => {
  return props.items.find((item) => item.id === currentScene.value) ?? null
})

const displayTitle = computed(() => {
  if (matchedGalleryItem.value) return matchedGalleryItem.value.title
  return isEn.value ? SCENE_LABELS_EN[currentScene.value] : SCENE_LABELS_ZH[currentScene.value]
})

const displayIcon = computed(() => matchedGalleryItem.value?.icon ?? 'i-lucide-smartphone')

const displayCaption = computed(() => {
  if (matchedGalleryItem.value) return matchedGalleryItem.value.caption
  return isEn.value
    ? 'Matches real BigSmart gestures & keys: swipe down from top for Control Center, swipe Home horizontally, IO0 BOOT returns Home, IO10 single/double/long press opens Control Center / Smart / Lock.'
    : '还原真机交互：顶部下滑打开控制中心、桌面/日历左右滑动翻页；下方按键对应 POWER（息屏）、IO0 BOOT（单击回桌面）、IO10（单击控制中心 / 双击 Smart / 长按锁屏）。'
})

function handleSceneChange(next: ScreenSceneId): void {
  currentScene.value = next
  if (props.items.some((item) => item.id === next)) {
    emit('update:activeId', next)
  }
}

function handleThemeChange(next: FirmwareThemeId): void {
  firmwareTheme.value = next
}

// Authentic IO10 single (280 ms delay) / double / long-press (550 ms) arbitration matching button_binding_service.cc
let io10SingleTimer: ReturnType<typeof setTimeout> | null = null
let io10LongTimer: ReturnType<typeof setTimeout> | null = null
let io10LongFired = false

function getTargetScreen(target: 'inline' | 'modal'): ScreenExposeApi | null {
  return target === 'modal' ? modalScreenRef.value : screenRef.value
}

function onSwitchPointerDown(sw: 'power' | 'boot' | 'io10', target: 'inline' | 'modal'): void {
  activeSwitch.value = sw
  if (sw === 'io10') {
    io10LongFired = false
    if (io10LongTimer) clearTimeout(io10LongTimer)
    io10LongTimer = setTimeout(() => {
      io10LongFired = true
      if (io10SingleTimer) {
        clearTimeout(io10SingleTimer)
        io10SingleTimer = null
      }
      getTargetScreen(target)?.lockDevice(true)
    }, 550)
  }
}

function onSwitchPointerUp(sw: 'power' | 'boot' | 'io10', target: 'inline' | 'modal'): void {
  if (activeSwitch.value === sw) activeSwitch.value = null
  const api = getTargetScreen(target)
  if (!api) return

  if (sw === 'power') {
    api.togglePowerSleep()
    return
  }
  if (sw === 'boot') {
    api.goHome(true)
    return
  }

  // IO10 release handling
  if (io10LongTimer) {
    clearTimeout(io10LongTimer)
    io10LongTimer = null
  }
  if (io10LongFired) {
    io10LongFired = false
    return
  }
  if (io10SingleTimer) {
    clearTimeout(io10SingleTimer)
    io10SingleTimer = null
    api.launchSmartFromButton()
  } else {
    io10SingleTimer = setTimeout(() => {
      io10SingleTimer = null
      api.toggleControlCenter(true)
    }, 280)
  }
}

function onSwitchPointerCancel(): void {
  activeSwitch.value = null
  if (io10LongTimer) {
    clearTimeout(io10LongTimer)
    io10LongTimer = null
  }
}

onBeforeUnmount(() => {
  if (io10SingleTimer) clearTimeout(io10SingleTimer)
  if (io10LongTimer) clearTimeout(io10LongTimer)
})

const ledOuterStyle = computed(() => ({
  left: '84.91%',
  top: '86.63%',
  width: '4.4%',
  height: '5.3%',
  backgroundColor: boardLedColor.value === 'transparent' ? 'transparent' : boardLedColor.value,
  opacity: boardLedColor.value === 'transparent' ? 0 : Math.min(0.85, boardLedIntensity.value)
}))

const ledCoreStyle = computed(() => ({
  left: '84.91%',
  top: '86.63%',
  width: '1.9%',
  height: '2.3%',
  backgroundColor: boardLedColor.value === 'transparent' ? 'transparent' : '#FFFFFF',
  opacity: boardLedColor.value === 'transparent' ? 0 : Math.min(0.9, boardLedIntensity.value)
}))

const physicalSwitches = computed(() => [
  {
    id: 'power' as const,
    left: '30.19%',
    shortLabel: 'PWR',
    title: isEn.value
      ? 'KEY_POWER (STC8G1K08A MCU) · Hardware Sleep / Wake'
      : 'KEY_POWER（STC8G 电源键）· 息屏 / 亮屏'
  },
  {
    id: 'boot' as const,
    left: '41.51%',
    shortLabel: 'BOOT',
    title: isEn.value
      ? 'IO0 BOOT · Single Click -> Home'
      : 'IO0 BOOT 键 · 单击回到桌面'
  },
  {
    id: 'io10' as const,
    left: '52.83%',
    shortLabel: 'IO10',
    title: isEn.value
      ? 'IO10 KEY · Single: Control Center · Double: Smart · Long: Lock'
      : 'IO10 键 · 单击控制中心 / 双击 Smart / 长按锁屏'
  }
])
</script>

<template>
  <div class="overflow-hidden rounded-2xl border border-default bg-elevated/45 shadow-2xl">
    <!-- 严格按 3d/enclosure/params.py (106 x 86 mm, R=8 mm) 还原的可交互真机正面外壳舞台 -->
    <div
      class="relative flex flex-col items-center justify-center bg-gradient-to-b from-elevated/60 to-default/90 px-4 py-6 sm:px-8 sm:py-8"
      :class="compact ? 'py-5 sm:py-6' : ''"
    >
      <div class="relative w-full" :class="compact ? 'max-w-[460px]' : 'max-w-[580px]'">
        <!-- 真实 106.0 x 86.0 mm front_shell 外壳轮廓 -->
        <div
          class="relative aspect-[106/86] w-full overflow-hidden rounded-[7.55%/9.30%] border border-slate-600/80 bg-gradient-to-b from-[#334155] via-[#273449] to-[#1e293b] shadow-[0_28px_64px_-18px_rgba(2,6,23,0.9),inset_0_1px_0_rgba(255,255,255,0.16),inset_0_-2px_6px_rgba(0,0,0,0.45)]"
        >
          <div
            class="pointer-events-none absolute inset-x-0 top-[5.5%] flex items-center justify-between px-[7.5%] opacity-60"
            aria-hidden="true"
          >
            <span class="font-edix text-[10px] tracking-[0.18em] text-slate-300">RODAKOS</span>
            <span class="font-mono text-[9px] tracking-wider text-slate-300">
              106×86 mm · LIVE 320×240
            </span>
          </div>

          <!-- 58.2 x 43.8 mm 真实显示窗开孔，挂载矢量可交互 RodakOsScreen -->
          <div
            class="absolute rounded-[1.8%] bg-gradient-to-br from-slate-950 via-slate-900 to-slate-700/80 p-[3px] shadow-[inset_0_2px_6px_rgba(0,0,0,0.95),0_1px_0_rgba(255,255,255,0.12)] sm:p-[4px]"
            style="left: 22.55%; top: 19.3%; width: 54.91%; height: 50.93%"
          >
            <div class="relative h-full w-full overflow-hidden rounded-[2px] bg-black">
              <LandingRodakOsScreen
                ref="screenRef"
                :scene="currentScene"
                :theme="firmwareTheme"
                @update:scene="handleSceneChange"
                @update:theme="handleThemeChange"
              />
            </div>
          </div>

          <!-- MIC1 (-33.0 mm) -->
          <span
            aria-hidden="true"
            title="MIC1 (-33.0 mm)"
            class="pointer-events-none absolute -translate-x-1/2 -translate-y-1/2 rounded-full bg-slate-950 shadow-[inset_0_1px_2px_rgba(0,0,0,0.95),0_1px_0_rgba(255,255,255,0.1)]"
            style="left: 18.87%; top: 86.63%; width: 1.25%; height: 3.4%"
          />

          <!-- 真实三枚非对称实体按键：POWER (-21mm), BOOT/IO0 (-9mm), IO10 (+3mm) -->
          <button
            v-for="sw in physicalSwitches"
            :key="sw.id"
            type="button"
            :title="sw.title"
            :aria-label="sw.title"
            class="group absolute flex -translate-x-1/2 -translate-y-1/2 cursor-pointer items-center justify-center rounded-full bg-slate-950/95 shadow-[inset_0_1px_3px_rgba(0,0,0,0.95),0_1px_0_rgba(255,255,255,0.14)] focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-sky-400"
            :style="{ left: sw.left, top: '86.63%', width: '4.8%', height: '5.9%' }"
            @pointerdown="onSwitchPointerDown(sw.id, 'inline')"
            @pointerup="onSwitchPointerUp(sw.id, 'inline')"
            @pointerleave="onSwitchPointerCancel"
            @pointercancel="onSwitchPointerCancel"
          >
            <span
              class="size-[72%] rounded-full bg-gradient-to-b from-slate-600 via-slate-800 to-slate-950 shadow-[0_2px_3px_rgba(0,0,0,0.85),inset_0_1px_0_rgba(255,255,255,0.28)] transition-transform duration-100 group-hover:from-sky-500/80 group-hover:via-slate-700"
              :class="activeSwitch === sw.id ? 'scale-85' : ''"
            />
          </button>

          <!-- MIC2 (+33.0 mm) -->
          <span
            aria-hidden="true"
            title="MIC2 (+33.0 mm)"
            class="pointer-events-none absolute -translate-x-1/2 -translate-y-1/2 rounded-full bg-slate-950 shadow-[inset_0_1px_2px_rgba(0,0,0,0.95),0_1px_0_rgba(255,255,255,0.1)]"
            style="left: 81.13%; top: 86.63%; width: 1.25%; height: 3.4%"
          />

          <!-- LED1 0.60mm 隐形柔光透光仓 (+37.0 mm) -->
          <span
            aria-hidden="true"
            title="LED1 0.60mm diffused RGB (+37.0 mm)"
            class="pointer-events-none absolute -translate-x-1/2 -translate-y-1/2 rounded-full blur-[4px] transition-all duration-300"
            :style="ledOuterStyle"
          />
          <span
            aria-hidden="true"
            class="pointer-events-none absolute -translate-x-1/2 -translate-y-1/2 rounded-full blur-[1.5px] transition-all duration-300"
            :style="ledCoreStyle"
          />

          <!-- 15° 桌面底座前挡沿 -->
          <div
            aria-hidden="true"
            class="pointer-events-none absolute inset-x-0 bottom-0 h-[6.2%] border-t border-slate-500/35 bg-gradient-to-b from-slate-900/95 to-slate-950 shadow-[0_-2px_8px_rgba(0,0,0,0.45)]"
          />
        </div>

        <div
          aria-hidden="true"
          class="mx-auto mt-1 h-2.5 w-[92%] rounded-full bg-slate-950/55 blur-md dark:bg-black/75"
        />
      </div>

      <!-- 底部实时控制条：4 套固件主题 + 真机按键与手势说明 -->
      <div class="mt-3.5 flex flex-col items-center gap-2">
        <div class="flex flex-wrap items-center justify-center gap-2">
          <span class="text-xs text-muted">{{ copy.hero.showcase.themeLabel }}</span>
          <div class="inline-flex items-center gap-1 rounded-full border border-default bg-default/80 p-1">
            <button
              v-for="opt in firmwareThemeOptions"
              :key="opt.id"
              type="button"
              class="inline-flex cursor-pointer items-center gap-1.5 rounded-full px-2.5 py-1 text-xs font-medium transition-colors"
              :class="
                firmwareTheme === opt.id
                  ? 'bg-primary/15 text-primary'
                  : 'text-muted hover:text-highlighted'
              "
              @click="handleThemeChange(opt.id)"
            >
              <span class="size-2.5 rounded-full" :class="opt.dotClass" />
              <span>{{ copy.hero.showcase.themeNames[opt.id] }}</span>
            </button>
          </div>
        </div>

        <div class="hidden items-center gap-1.5 text-[11px] text-dimmed sm:inline-flex">
          <UIcon name="i-lucide-hand" class="size-3.5 text-primary" />
          <span>
            {{
              isEn
                ? 'Top swipe-down: Control Center · Swipe Home · BOOT: Home · IO10: Click CC / Double Smart / Long Lock'
                : '顶部下滑开控制中心 · 左右滑翻页 · BOOT 回桌面 · IO10 单击控制中心 / 双击 Smart / 长按锁屏'
            }}
          </span>
        </div>
      </div>
    </div>

    <!-- 底部元数据与说明栏 -->
    <div
      class="flex flex-col gap-2.5 border-t border-default bg-elevated/65 px-4 py-3 text-left sm:flex-row sm:items-center sm:justify-between"
    >
      <div class="min-w-0 space-y-1">
        <div class="flex flex-wrap items-center gap-2">
          <UIcon :name="displayIcon" class="size-3.5 shrink-0 text-primary" />
          <span class="text-xs font-medium text-highlighted">{{ displayTitle }}</span>
          <UBadge
            :label="copy.hero.showcase.realRenderBadge"
            color="primary"
            variant="subtle"
            size="xs"
          />
        </div>
        <p class="text-xs leading-relaxed text-muted">
          {{ displayCaption }}
        </p>
      </div>

      <div class="flex shrink-0 items-center justify-between gap-2 sm:justify-end">
        <UButton
          v-if="currentScene !== 'home'"
          :label="isEn ? 'Home' : '回到桌面'"
          icon="i-lucide-home"
          color="neutral"
          variant="ghost"
          size="xs"
          @click="screenRef?.goHome(false)"
        />
        <UButton
          :label="copy.hero.showcase.expandLabel"
          icon="i-lucide-maximize-2"
          color="neutral"
          variant="outline"
          size="xs"
          @click="modalOpen = true"
        />
      </div>
    </div>

    <!-- 全屏大尺寸可交互原型弹窗 -->
    <UModal
      v-model:open="modalOpen"
      :title="displayTitle"
      :description="displayCaption"
      :ui="{
        content: 'w-[calc(100vw-1.5rem)] max-w-4xl',
        body: 'p-4 sm:p-6',
        footer: 'flex-col items-stretch gap-3 sm:flex-row sm:items-center sm:justify-between'
      }"
    >
      <template #body>
        <div v-if="modalOpen" class="space-y-4">
          <div
            class="mx-auto w-full max-w-[680px] overflow-hidden rounded-xl border border-default bg-black shadow-2xl ring-1 ring-primary/20"
          >
            <LandingRodakOsScreen
              ref="modalScreenRef"
              :scene="currentScene"
              :theme="firmwareTheme"
              @update:scene="handleSceneChange"
              @update:theme="handleThemeChange"
            />
          </div>

          <!-- 真机物理键快捷触发栏 -->
          <div class="flex flex-wrap items-center justify-center gap-2">
            <UButton
              :label="isEn ? 'IO0 BOOT · Home' : 'IO0 BOOT · 回到桌面'"
              icon="i-lucide-home"
              color="neutral"
              variant="soft"
              size="xs"
              @click="modalScreenRef?.goHome(true)"
            />
            <UButton
              :label="isEn ? 'IO10 Single · Control Center' : 'IO10 单击 · 控制中心'"
              icon="i-lucide-sliders-horizontal"
              color="neutral"
              variant="soft"
              size="xs"
              @click="modalScreenRef?.toggleControlCenter(true)"
            />
            <UButton
              :label="isEn ? 'IO10 Double · Smart' : 'IO10 双击 · 智能家居'"
              icon="i-lucide-lightbulb"
              color="neutral"
              variant="soft"
              size="xs"
              @click="modalScreenRef?.launchSmartFromButton()"
            />
            <UButton
              :label="isEn ? 'IO10 Long · Lock' : 'IO10 长按 · 锁屏'"
              icon="i-lucide-lock"
              color="neutral"
              variant="soft"
              size="xs"
              @click="modalScreenRef?.lockDevice(true)"
            />
            <div class="h-4 w-px bg-default" />
            <UButton
              v-for="slide in items"
              :key="slide.id"
              :label="slide.label"
              :icon="slide.icon"
              :color="currentScene === slide.id ? 'primary' : 'neutral'"
              :variant="currentScene === slide.id ? 'soft' : 'ghost'"
              size="xs"
              @click="handleSceneChange(slide.id)"
            />
          </div>
        </div>
      </template>

      <template #footer>
        <div class="flex flex-wrap items-center gap-2">
          <span class="text-xs text-muted">{{ copy.hero.showcase.themeLabel }}</span>
          <div class="inline-flex items-center gap-1 rounded-full border border-default bg-default/80 p-0.5">
            <button
              v-for="opt in firmwareThemeOptions"
              :key="opt.id"
              type="button"
              class="inline-flex cursor-pointer items-center gap-1 rounded-full px-2 py-0.5 text-xs font-medium"
              :class="firmwareTheme === opt.id ? 'bg-primary/15 text-primary' : 'text-muted'"
              @click="handleThemeChange(opt.id)"
            >
              <span class="size-2 rounded-full" :class="opt.dotClass" />
              <span>{{ copy.hero.showcase.themeNames[opt.id] }}</span>
            </button>
          </div>
        </div>
        <UButton
          :label="copy.hero.showcase.closeLabel"
          color="neutral"
          variant="soft"
          size="xs"
          @click="modalOpen = false"
        />
      </template>
    </UModal>
  </div>
</template>
