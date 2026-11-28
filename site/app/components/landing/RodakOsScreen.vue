<script setup lang="ts">
import type { RodakosIonName } from '~/data/rodakos-icons'
import {
  CALENDAR_MONTHS_2026,
  RODAKOS_THEMES,
  darkenHex,
  type CalendarCellData,
  type IconSlotName
} from '~/data/rodakos-themes'
import { SMART_COLOR_SWATCHES } from '~/composables/useRodakDemoOs'
import type { FirmwareThemeId, ScreenSceneId } from '~/types/landing'

const props = withDefaults(
  defineProps<{
    scene: ScreenSceneId
    theme?: FirmwareThemeId
    interactive?: boolean
  }>(),
  {
    theme: 'dark',
    interactive: true
  }
)

const emit = defineEmits<{
  'update:scene': [scene: ScreenSceneId]
  'update:theme': [theme: FirmwareThemeId]
  'led-pulse': [color: string]
}>()

const {
  brightness,
  wakeEnabled,
  voiceLedEnabled,
  lockOnBoot,
  controlCenterGestureEnabled,
  publisherTrusted,
  unreadMessages,
  openTodos,
  lights
} = useRodakDemoOs()

// Responsive vector scaling: exact 320x240 logical coordinate space at native browser DPI
const viewportRef = useTemplateRef<HTMLDivElement>('viewportRef')
const scale = shallowRef(1)
let ro: ResizeObserver | null = null

function updateScale(): void {
  const el = viewportRef.value
  if (!el) return
  const w = el.clientWidth
  if (w > 0) scale.value = w / 320
}

// =========================================================================
// SHELL STATE (lv_layer_top(): Control Center, Lock Screen, Voice Bar, Toast)
// =========================================================================
const controlCenterOpen = shallowRef(false)
const lockScreenOpen = shallowRef(false)
const screenSleeping = shallowRef(false)
const voiceOverlayText = shallowRef('')
const toastText = shallowRef('')
let toastTimer: ReturnType<typeof setTimeout> | null = null

// App sub-view state hoisted before watch() to avoid TDZ
const selectedLightId = shallowRef<string | null>(null)
const selectedPhotoIdx = shallowRef<number | null>(null)
const calDayViewCell = ref<CalendarCellData | null>(null)
const settingsSubPage = shallowRef<'main' | 'buttons' | 'shell'>('main')
const selectedMessageId = shallowRef<string | null>(null)

function showToast(msg: string): void {
  toastText.value = msg
  if (toastTimer) clearTimeout(toastTimer)
  toastTimer = setTimeout(() => {
    toastText.value = ''
  }, 1600)
}

watch(
  () => props.scene,
  (next) => {
    controlCenterOpen.value = false
    lockScreenOpen.value = false
    screenSleeping.value = false
    if (next === 'voice-caption') {
      voiceOverlayText.value = '已打开客厅主灯，亮度 60%'
    } else if (next !== 'home' && next !== 'assistant') {
      voiceOverlayText.value = ''
    }
    if (next !== 'smart') selectedLightId.value = null
    if (next !== 'photos') selectedPhotoIdx.value = null
    if (next !== 'calendar') calDayViewCell.value = null
    if (next !== 'settings') settingsSubPage.value = 'main'
    if (next !== 'messages') selectedMessageId.value = null
  },
  { immediate: true }
)

// =========================================================================
// REAL TOUCH GESTURES (PhoneShell top pull-down / bottom swipe-up + Home/Cal horizontal swipe)
// =========================================================================
let gStartX = 0
let gStartY = 0
let gActive = false
let suppressClickUntil = 0

function isGestureBlocked(): boolean {
  return Date.now() < suppressClickUntil
}

function toLogicalPoint(e: PointerEvent): { x: number; y: number } | null {
  const el = viewportRef.value
  if (!el) return null
  const rect = el.getBoundingClientRect()
  if (rect.width <= 0 || rect.height <= 0) return null
  return {
    x: ((e.clientX - rect.left) / rect.width) * 320,
    y: ((e.clientY - rect.top) / rect.height) * 240
  }
}

function onScreenPointerDown(e: PointerEvent): void {
  if (!props.interactive || screenSleeping.value) return
  if ((e.target as HTMLElement | null)?.tagName === 'INPUT') return
  const pt = toLogicalPoint(e)
  if (!pt) return
  gStartX = pt.x
  gStartY = pt.y
  gActive = true
}

function onScreenPointerUp(e: PointerEvent): void {
  if (!gActive || !props.interactive) return
  gActive = false
  const pt = toLogicalPoint(e)
  if (!pt) return
  const dx = pt.x - gStartX
  const dy = pt.y - gStartY
  const absX = Math.abs(dx)
  const absY = Math.abs(dy)
  const vertical = absY > absX

  // 1. PhoneShell top-edge swipe down -> Control Center (kTopGestureBand=20, relaxed to 28 for desktop)
  if (
    !lockScreenOpen.value &&
    !controlCenterOpen.value &&
    controlCenterGestureEnabled.value &&
    gStartY <= 28 &&
    vertical &&
    dy >= 36
  ) {
    suppressClickUntil = Date.now() + 260
    controlCenterOpen.value = true
    return
  }

  // 2. Control Center swipe up -> Close
  if (controlCenterOpen.value && vertical && dy <= -36) {
    suppressClickUntil = Date.now() + 260
    controlCenterOpen.value = false
    return
  }

  // 3. Lock Screen bottom band swipe up -> Unlock (kBottomGestureBand=64 -> y >= 176)
  if (lockScreenOpen.value && gStartY >= 165 && vertical && dy <= -36) {
    suppressClickUntil = Date.now() + 260
    lockScreenOpen.value = false
    return
  }

  // 4. Home horizontal tileview swipe (Page 1 <-> Page 2)
  if (!lockScreenOpen.value && !controlCenterOpen.value && isHomeView.value && !vertical && absX >= 38) {
    suppressClickUntil = Date.now() + 260
    if (dx < 0 && homePage.value === 1) {
      openScene('home-page2')
    } else if (dx > 0 && homePage.value === 2) {
      openScene('home')
    }
    return
  }

  // 5. Calendar Month view horizontal swipe (kSwipeThreshold = 40)
  if (
    !lockScreenOpen.value &&
    !controlCenterOpen.value &&
    props.scene === 'calendar' &&
    !calDayViewCell.value &&
    !vertical &&
    absX >= 38
  ) {
    suppressClickUntil = Date.now() + 260
    stepCalendarMonth(dx < 0 ? 1 : -1)
  }
}

const t = computed(() => RODAKOS_THEMES[props.theme] ?? RODAKOS_THEMES.dark)

const isHomeView = computed(
  () => props.scene === 'home' || props.scene === 'home-page2' || props.scene === 'voice-caption'
)
const homePage = computed<1 | 2>(() => (props.scene === 'home-page2' ? 2 : 1))

interface HomeTileDef {
  id: ScreenSceneId
  title: string
  icon: RodakosIonName
  slot: IconSlotName
}

const HOME_PAGE1_APPS: readonly HomeTileDef[] = [
  { id: 'settings', title: 'Settings', icon: 'settings-outline', slot: 'system' },
  { id: 'photos', title: 'Photos', icon: 'image-outline', slot: 'warm' },
  { id: 'camera', title: 'Camera', icon: 'camera-outline', slot: 'media' },
  { id: 'clock', title: 'Clock', icon: 'time-outline', slot: 'warm' },
  { id: 'calendar', title: 'Calendar', icon: 'calendar-number-outline', slot: 'tools' },
  { id: 'files', title: 'Files', icon: 'folder-outline', slot: 'neutral' },
  { id: 'gyro', title: 'Gyro', icon: 'compass-outline', slot: 'tools' },
  { id: 'system', title: 'System', icon: 'information-circle-outline', slot: 'system' },
  { id: 'music', title: 'Music', icon: 'musical-notes-outline', slot: 'media' },
  { id: 'recorder', title: 'Recorder', icon: 'mic-outline', slot: 'media' },
  { id: 'assistant', title: 'Assistant', icon: 'chatbubble-ellipses-outline', slot: 'cool' },
  { id: 'smart', title: 'Smart', icon: 'bulb-outline', slot: 'cool' }
]

const HOME_PAGE2_APPS: readonly HomeTileDef[] = [
  { id: 'wol', title: 'Wake', icon: 'power-outline', slot: 'alert' },
  { id: 'themes', title: 'Themes', icon: 'color-palette-outline', slot: 'system' },
  { id: 'messages', title: 'Messages', icon: 'notifications-outline', slot: 'alert' }
]

const visibleHomeApps = computed(() => (homePage.value === 1 ? HOME_PAGE1_APPS : HOME_PAGE2_APPS))

function tileGradient(slot: IconSlotName): string {
  const top = t.value.slots[slot]
  const bot = darkenHex(top, 31 / 255)
  return `linear-gradient(180deg, ${top} 0%, ${bot} 100%)`
}

function openScene(next: ScreenSceneId): void {
  if (!props.interactive || isGestureBlocked()) return
  if (lockScreenOpen.value) return
  controlCenterOpen.value = false
  if (next !== 'voice-caption') {
    voiceOverlayText.value = ''
  }
  emit('update:scene', next)
}

function goHome(fromButton = false): void {
  if (!props.interactive) return
  if (screenSleeping.value) {
    screenSleeping.value = false
    return
  }
  if (lockScreenOpen.value) {
    if (fromButton) showToast('Device is locked')
    return
  }
  controlCenterOpen.value = false
  voiceOverlayText.value = ''
  emit('update:scene', 'home')
  if (fromButton) showToast('IO0 Boot: Home')
}

function toggleControlCenter(fromButton = false): void {
  if (!props.interactive) return
  if (screenSleeping.value) {
    screenSleeping.value = false
    return
  }
  if (lockScreenOpen.value) return
  controlCenterOpen.value = !controlCenterOpen.value
  if (fromButton) showToast('IO10 Key: Control Center')
}

function launchSmartFromButton(): void {
  if (!props.interactive) return
  if (screenSleeping.value) screenSleeping.value = false
  if (lockScreenOpen.value) return
  controlCenterOpen.value = false
  voiceOverlayText.value = ''
  emit('update:scene', 'smart')
  showToast('IO10 Key: Open Smart')
}

function lockDevice(fromButton = false): void {
  if (!props.interactive) return
  if (screenSleeping.value) screenSleeping.value = false
  controlCenterOpen.value = false
  lockScreenOpen.value = true
  if (fromButton) showToast('IO10 Key: Lock')
}

function togglePowerSleep(): void {
  if (!props.interactive) return
  screenSleeping.value = !screenSleeping.value
  if (!screenSleeping.value && lockOnBoot.value) {
    lockScreenOpen.value = true
  }
}

defineExpose({
  goHome,
  toggleControlCenter,
  launchSmartFromButton,
  lockDevice,
  togglePowerSleep
})

// =========================================================================
// APP 1: CALENDAR (Month swipe + authentic Day View on cell click)
// =========================================================================
const calMonthIdx = shallowRef(1) // Nov 2026
const currentMonth = computed(() => CALENDAR_MONTHS_2026[calMonthIdx.value] ?? CALENDAR_MONTHS_2026[1]!)

function stepCalendarMonth(delta: -1 | 1): void {
  const next = calMonthIdx.value + delta
  if (next >= 0 && next < CALENDAR_MONTHS_2026.length) {
    calMonthIdx.value = next
    calDayViewCell.value = null
  }
}

function openCalendarDay(cell: CalendarCellData): void {
  if (isGestureBlocked()) return
  calDayViewCell.value = cell
}

function jumpCalendarToday(): void {
  calMonthIdx.value = 1
  calDayViewCell.value = null
}

function relativeDayLabel(cell: CalendarCellData): string {
  if (cell.today) return '今天'
  if (calMonthIdx.value === 1 && !cell.out) {
    const diff = cell.d - 26
    if (diff === 1) return '明天'
    if (diff === -1) return '昨天'
    return diff > 0 ? `${diff} 天后` : `${-diff} 天前`
  }
  return currentMonth.value.label
}

// =========================================================================
// APP 2: GYRO (154x154 Bubble Level + Six-Axis Centered Bars)
// =========================================================================
const gyroRoll = shallowRef(18.4)
const gyroPitch = shallowRef(-9.2)
const gyroYaw = shallowRef(42)
const gyroZeroRoll = shallowRef(0)
const gyroZeroPitch = shallowRef(0)
const gyroFrozen = shallowRef(false)
const gyroBarsMode = shallowRef(false)

const displayRoll = computed(() => +(gyroRoll.value - gyroZeroRoll.value).toFixed(1))
const displayPitch = computed(() => +(gyroPitch.value - gyroZeroPitch.value).toFixed(1))
const isLevel = computed(() => Math.abs(displayRoll.value) < 2.2 && Math.abs(displayPitch.value) < 2.2)

const bubblePos = computed(() => {
  const clamp = (v: number, lim: number) => Math.max(-lim, Math.min(lim, v))
  return {
    x: 75 + clamp(displayRoll.value * 1.15, 48),
    y: 75 + clamp(displayPitch.value * 1.15, 48)
  }
})

const horizonLine = computed(() => {
  const rad = (displayRoll.value * Math.PI) / 180
  const dx = Math.cos(rad) * 44
  const dy = Math.sin(rad) * 44
  return { x1: 75 - dx, y1: 75 - dy, x2: 75 + dx, y2: 75 + dy }
})

function handleDialPointer(e: PointerEvent): void {
  if (!props.interactive || gyroFrozen.value) return
  const target = e.currentTarget as HTMLElement | null
  if (!target) return
  const rect = target.getBoundingClientRect()
  const nx = ((e.clientX - rect.left) / rect.width) * 2 - 1
  const ny = ((e.clientY - rect.top) / rect.height) * 2 - 1
  gyroRoll.value = +(nx * 36 + gyroZeroRoll.value).toFixed(1)
  gyroPitch.value = +(ny * 36 + gyroZeroPitch.value).toFixed(1)
  gyroYaw.value = Math.round(nx * 65 + 28)
}

function zeroGyro(): void {
  gyroZeroRoll.value = gyroRoll.value
  gyroZeroPitch.value = gyroPitch.value
}

// =========================================================================
// APP 3: SMART LIGHTS (Whole-hero toggle, off-color save without turning on)
// =========================================================================
const selectedLight = computed(() =>
  selectedLightId.value ? lights.value.find((l) => l.id === selectedLightId.value) ?? null : null
)

function toggleLight(id: string): void {
  const item = lights.value.find((l) => l.id === id)
  if (!item) return
  item.on = !item.on
  if (id === 'board') {
    emit('led-pulse', item.on ? item.color : 'transparent')
  }
}

// Matches real SmartApp: picking color while OFF saves color without powering light on
function setLightColor(id: string, color: string, colorName: string): void {
  const item = lights.value.find((l) => l.id === id)
  if (!item) return
  item.color = color
  item.colorName = colorName
  if (id === 'board' && item.on) {
    emit('led-pulse', color)
  }
}

// =========================================================================
// APP 4: ASSISTANT (Wake -> Thinking -> Shell lv_layer_top() Voice Caption)
// =========================================================================
const assistantState = shallowRef<'idle' | 'thinking' | 'speaking'>('idle')
let assistantTimer: ReturnType<typeof setTimeout> | null = null

function triggerAssistantDemo(): void {
  if (!props.interactive) return
  if (assistantTimer) clearTimeout(assistantTimer)
  assistantState.value = 'thinking'
  emit('led-pulse', '#38bdf8')
  assistantTimer = setTimeout(() => {
    assistantState.value = 'speaking'
    voiceOverlayText.value = '已打开客厅主灯，亮度 60%'
    assistantTimer = setTimeout(() => {
      assistantState.value = 'idle'
    }, 3200)
  }, 1100)
}

// =========================================================================
// APP 5: RECORDER (Live timer + 20-bar animated 48 dB level meter)
// =========================================================================
const isRecording = shallowRef(true)
const recSeconds = shallowRef(1)
const recNotice = shallowRef('')
const meterLevels = ref<number[]>(Array.from({ length: 20 }, (_, i) => (i >= 11 ? 26 : 3)))
let recTimer: ReturnType<typeof setInterval> | null = null

function startRecLoop(): void {
  if (recTimer) clearInterval(recTimer)
  let tick = 0
  recTimer = setInterval(() => {
    if (props.scene !== 'recorder' || !isRecording.value) return
    tick += 1
    if (tick % 5 === 0) recSeconds.value += 1
    const peak = 6 + Math.floor(Math.random() * 13)
    meterLevels.value = Array.from({ length: 20 }, (_, i) => {
      if (i < 20 - peak) return 3
      return 12 + Math.floor(Math.random() * 16)
    })
  }, 200)
}

function toggleRecording(): void {
  isRecording.value = !isRecording.value
  if (isRecording.value) {
    recNotice.value = ''
    startRecLoop()
  } else {
    recNotice.value = 'WAV 已安全封尾保存'
    meterLevels.value = Array.from({ length: 20 }, () => 3)
  }
}

const recTimeLabel = computed(() => {
  const m = Math.floor(recSeconds.value / 60)
  const s = recSeconds.value % 60
  return `${m}:${String(s).padStart(2, '0')}`
})

// =========================================================================
// APP 6: WAKE-ON-LAN
// =========================================================================
const wolHosts = ref([
  { id: 'pc', name: '书房台式机', mac: 'AA:BB:CC:DD:EE:FF · 端口 9', sent: false },
  { id: 'nas', name: '客厅 NAS', mac: 'D8:3A:DD:42:19:0C · 端口 9', sent: false }
])

function sendWol(id: string): void {
  const host = wolHosts.value.find((h) => h.id === id)
  if (!host) return
  host.sent = true
  showToast(`已唤醒 ${host.name}`)
  setTimeout(() => {
    host.sent = false
  }, 1600)
}

function addDemoWolHost(): void {
  if (wolHosts.value.length >= 3) return
  wolHosts.value.push({
    id: 'lab',
    name: '实验室主机',
    mac: '4C:CC:6A:12:8B:F0 · 端口 9',
    sent: false
  })
}

// =========================================================================
// APP 7: CLOCK, MUSIC, CAMERA, PHOTOS, MESSAGES, FILES
// =========================================================================
const clockNow = shallowRef('10:08')
const clockSec = shallowRef('26')
const sntpStatus = shallowRef<'Synced' | 'Syncing...'>('Synced')
let clockInterval: ReturnType<typeof setInterval> | null = null

function triggerClockSync(): void {
  sntpStatus.value = 'Syncing...'
  setTimeout(() => {
    sntpStatus.value = 'Synced'
    showToast('Time synced')
  }, 600)
}

const musicPlaying = shallowRef(true)
const musicTrackIdx = shallowRef(0)
const musicProgress = shallowRef(46)
const musicMode = shallowRef<'顺序' | '单曲'>('顺序')
const MUSIC_TRACKS = [
  { title: 'Midnight Lo-Fi', artist: 'Rodak Demo · WAV', dur: '2:49' },
  { title: 'Warm Chime', artist: 'Rodak Audio · MP3', dur: '1:52' }
] as const

const cameraFlash = shallowRef(false)
const cameraShots = shallowRef(14)

const DEMO_PHOTOS = [
  { bg: 'linear-gradient(135deg, #0284c7, #1e293b)', label: 'IMG_0014.JPG', date: '今天 09:42' },
  { bg: 'linear-gradient(135deg, #d97706, #451a03)', label: 'DESK_02.JPG', date: '今天 08:15' },
  { bg: 'linear-gradient(135deg, #059669, #064e3b)', label: 'GARDEN.PNG', date: '11月24日' },
  { bg: 'linear-gradient(135deg, #7c3aed, #1e1b4b)', label: 'NIGHT_09.JPG', date: '11月22日' },
  { bg: 'linear-gradient(135deg, #e11d48, #4c0519)', label: 'BOARD_V6.JPG', date: '11月20日' },
  { bg: 'linear-gradient(135deg, #0d9488, #134e4a)', label: 'PRINT_03.JPG', date: '11月18日' },
  { bg: 'linear-gradient(135deg, #4f46e5, #1e1b4b)', label: 'LAB_TEST.PNG', date: '11月15日' },
  { bg: 'linear-gradient(135deg, #ca8a04, #422006)', label: 'SUNSET.JPG', date: '11月12日' }
] as const

const msgFilter = shallowRef<'all' | 'todo' | 'notice'>('all')
const messagesList = ref([
  {
    id: 'm1',
    kind: 'todo' as const,
    title: '更换 3D 打印耗材',
    body: '确认 PLA 料盘余量足够完成 6 件外壳与底座打印。',
    time: '10:02',
    state: 'open' as 'open' | 'done' | 'snoozed'
  },
  {
    id: 'm2',
    kind: 'notice' as const,
    title: 'Rodak 工作台已连接',
    body: 'HTTPS / MQTTS / WSS 三通道已共享固定 .local 证书完成验签。',
    time: '09:45',
    state: 'open' as 'open' | 'done' | 'snoozed'
  }
])

const filteredMessages = computed(() =>
  msgFilter.value === 'all'
    ? messagesList.value
    : messagesList.value.filter((m) => m.kind === msgFilter.value)
)
const activeMessage = computed(() =>
  selectedMessageId.value ? messagesList.value.find((m) => m.id === selectedMessageId.value) ?? null : null
)

function completeMessage(id: string): void {
  const m = messagesList.value.find((item) => item.id === id)
  if (m && m.state !== 'done') {
    m.state = 'done'
    unreadMessages.value = Math.max(0, unreadMessages.value - 1)
    if (m.kind === 'todo') openTodos.value = Math.max(0, openTodos.value - 1)
    showToast('已完成')
  }
}

function snoozeMessage(id: string): void {
  const m = messagesList.value.find((item) => item.id === id)
  if (m) {
    m.state = 'snoozed'
    showToast('已推迟 30 分钟')
  }
}

function captureCamera(): void {
  cameraFlash.value = true
  cameraShots.value += 1
  showToast('已保存至 /DCIM')
  setTimeout(() => {
    cameraFlash.value = false
  }, 180)
}

const selectedFileNote = shallowRef('')

const lockNoticeText = computed(() => {
  if (unreadMessages.value > 0 && openTodos.value > 0) {
    return `有 ${unreadMessages.value} 条新消息 · ${openTodos.value} 项待办`
  }
  if (unreadMessages.value > 0) return `有 ${unreadMessages.value} 条新消息`
  if (openTodos.value > 0) return `有 ${openTodos.value} 项待办`
  return ''
})

onMounted(() => {
  updateScale()
  if (viewportRef.value && typeof ResizeObserver !== 'undefined') {
    ro = new ResizeObserver(() => updateScale())
    ro.observe(viewportRef.value)
  }
  startRecLoop()
  clockInterval = setInterval(() => {
    const now = new Date()
    clockNow.value = `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`
    clockSec.value = String(now.getSeconds()).padStart(2, '0')
    if (props.scene === 'music' && musicPlaying.value) {
      musicProgress.value = musicProgress.value >= 100 ? 0 : musicProgress.value + 1
    }
  }, 1000)
})

onBeforeUnmount(() => {
  ro?.disconnect()
  if (recTimer) clearInterval(recTimer)
  if (clockInterval) clearInterval(clockInterval)
  if (assistantTimer) clearTimeout(assistantTimer)
  if (toastTimer) clearTimeout(toastTimer)
})
</script>

<template>
  <div
    ref="viewportRef"
    class="relative aspect-[4/3] w-full touch-none overflow-hidden select-none"
    :style="{ backgroundColor: t.bgPrimary }"
    @pointerdown="onScreenPointerDown"
    @pointerup="onScreenPointerUp"
  >
    <!-- Exact 320x240 logical coordinate canvas scaled crisply via browser vector engine -->
    <div
      class="rd-os-canvas origin-top-left"
      :style="{
        width: '320px',
        height: '240px',
        transform: `scale(${scale})`,
        backgroundColor: t.bgPrimary,
        color: t.textPrimary,
        filter: brightness < 100 ? `brightness(${0.48 + (brightness / 100) * 0.52})` : undefined
      }"
    >
      <!-- ==================== HOME (PAGE 1 & PAGE 2, LVGL TILEVIEW) ==================== -->
      <template v-if="isHomeView">
        <!-- Status Bar: clock (left), 8px unread dot at x=58 -> Messages, Wi-Fi + Battery right; drag down from top opens Control Center -->
        <div class="relative flex h-[24px] w-full items-center justify-between px-[14px] pt-[4px]">
          <div class="flex items-center gap-[8px]">
            <span class="text-[13.5px] font-medium tracking-[0.2px]" :style="{ color: t.textPrimary }">
              {{ clockNow }}
            </span>
            <button
              v-if="unreadMessages > 0"
              type="button"
              class="flex size-[18px] cursor-pointer items-center justify-center rounded-full"
              title="未读消息提示点 · 点击打开 Messages"
              @click.stop="openScene('messages')"
            >
              <span class="block size-[8px] rounded-full" :style="{ backgroundColor: t.error }" />
            </button>
          </div>
          <!-- Subtle top-center pull-down handle cue for Control Center -->
          <button
            type="button"
            class="absolute top-[2px] left-1/2 -translate-x-1/2 cursor-ns-resize px-[16px] py-[4px] opacity-35 transition-opacity hover:opacity-80"
            title="从顶部下滑（或点击）打开控制中心"
            @click="controlCenterGestureEnabled && toggleControlCenter(false)"
          >
            <span class="block h-[2.5px] w-[28px] rounded-full" :style="{ backgroundColor: t.textTertiary }" />
          </button>
          <div class="flex items-center gap-[6px]">
            <LandingRodakIonIcon name="wifi-outline" :size="17" :color="t.textPrimary" />
            <LandingRodakIonIcon name="battery-full-outline" :size="18" :color="t.success" />
            <span class="text-[12px] font-medium" :style="{ color: t.textTertiary }">92%</span>
          </div>
        </div>

        <!-- 4x3 App Grid (swipe horizontally left/right between Page 1 & Page 2) -->
        <div class="grid grid-cols-4 gap-x-[12px] gap-y-[6px] px-[14px] pt-[4px]">
          <button
            v-for="app in visibleHomeApps"
            :key="app.id"
            type="button"
            class="group relative flex h-[60px] w-[64px] flex-col items-center justify-start transition-transform active:scale-95"
            :class="interactive ? 'cursor-pointer' : 'cursor-default'"
            @click="openScene(app.id)"
          >
            <span
              class="flex size-[46px] items-center justify-center rounded-[14px] transition-transform duration-150 group-hover:scale-[1.05]"
              :style="{ background: tileGradient(app.slot) }"
            >
              <LandingRodakIonIcon :name="app.icon" :size="28" color="#FFFFFF" />
            </span>
            <span
              class="mt-[2px] text-center text-[11px] leading-[12px] whitespace-nowrap"
              :style="{ color: t.textSecondary }"
            >
              {{ app.title }}
            </span>
          </button>
        </div>

        <!-- Bottom Page Indicator Dots (swipe or click to flip pages) -->
        <div class="absolute right-0 bottom-[6px] left-0 flex items-center justify-center gap-[8px]">
          <button
            type="button"
            aria-label="Home Page 1"
            class="h-[6px] cursor-pointer rounded-full transition-all"
            :style="{
              width: homePage === 1 ? '18px' : '6px',
              backgroundColor: homePage === 1 ? t.secondary : t.border
            }"
            @click="openScene('home')"
          />
          <button
            type="button"
            aria-label="Home Page 2"
            class="h-[6px] cursor-pointer rounded-full transition-all"
            :style="{
              width: homePage === 2 ? '18px' : '6px',
              backgroundColor: homePage === 2 ? t.secondary : t.border
            }"
            @click="openScene('home-page2')"
          />
        </div>
      </template>

      <!-- ==================== CALENDAR APP (Month Grid + Real Day View) ==================== -->
      <template v-else-if="scene === 'calendar'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <div class="z-10 flex items-center gap-[6px]">
            <button
              type="button"
              class="flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
              :style="{ backgroundColor: t.bgTertiary }"
              @click="calDayViewCell ? (calDayViewCell = null) : goHome(false)"
            >
              <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
            </button>
            <button
              v-if="!calDayViewCell"
              type="button"
              class="flex h-[26px] w-[30px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95 disabled:opacity-40"
              :disabled="calMonthIdx === 0"
              :style="{ backgroundColor: t.bgTertiary }"
              @click="stepCalendarMonth(-1)"
            >
              <LandingRodakIonIcon name="chevron-back-outline" :size="15" :color="t.textPrimary" />
            </button>
          </div>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">
            {{ calDayViewCell ? `${currentMonth.label} ${calDayViewCell.d} 日` : currentMonth.label }}
          </div>
          <div v-if="!calDayViewCell" class="z-10 flex items-center gap-[6px]">
            <button
              type="button"
              class="flex h-[26px] w-[30px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95 disabled:opacity-40"
              :disabled="calMonthIdx === CALENDAR_MONTHS_2026.length - 1"
              :style="{ backgroundColor: t.bgTertiary }"
              @click="stepCalendarMonth(1)"
            >
              <LandingRodakIonIcon name="chevron-forward-outline" :size="15" :color="t.textPrimary" />
            </button>
            <button
              type="button"
              class="flex h-[26px] min-w-[44px] cursor-pointer items-center justify-center rounded-[7px] px-[8px] text-[12.5px] font-medium active:scale-95"
              :style="{ backgroundColor: t.bgTertiary, color: t.textPrimary }"
              @click="jumpCalendarToday"
            >
              今天
            </button>
          </div>
          <div v-else class="w-[38px]" />
        </div>

        <!-- Month View -->
        <template v-if="!calDayViewCell">
          <div class="grid grid-cols-7 px-[8px] pt-[3px] pb-[2px] text-center">
            <div
              v-for="(w, idx) in ['一', '二', '三', '四', '五', '六', '日']"
              :key="w"
              class="text-[12px] leading-[15px] font-medium"
              :style="{ color: idx >= 5 ? t.secondary : t.textTertiary }"
            >
              {{ w }}
            </div>
          </div>

          <div class="grid grid-cols-7 grid-rows-6 gap-x-[2px] gap-y-[1px] px-[8px] pt-[2px]">
            <button
              v-for="(c, idx) in currentMonth.cells"
              :key="`${currentMonth.key}-${idx}`"
              type="button"
              class="flex h-[29px] cursor-pointer flex-col items-center justify-center gap-[1px] rounded-[7px] transition-colors hover:opacity-90"
              :style="{ backgroundColor: c.today ? t.primary : 'transparent' }"
              @click="openCalendarDay(c)"
            >
              <span
                class="text-[12.5px] leading-[13px] font-semibold"
                :style="{
                  color: c.today
                    ? t.onPrimary
                    : c.out
                      ? c.wk
                        ? t.isLight
                          ? 'rgba(255,152,0,0.42)'
                          : 'rgba(242,199,110,0.34)'
                        : t.isLight
                          ? 'rgba(117,117,117,0.48)'
                          : 'rgba(190,212,225,0.32)'
                      : c.wk
                        ? t.secondary
                        : t.textPrimary
                }"
              >
                {{ c.d }}
              </span>
              <span
                class="text-[9.5px] leading-[10px]"
                :style="{
                  color: c.today
                    ? t.onPrimary
                    : c.out
                      ? t.isLight
                        ? 'rgba(158,158,158,0.45)'
                        : 'rgba(143,182,200,0.28)'
                      : c.term
                        ? t.secondary
                        : t.textTertiary
                }"
              >
                {{ c.l }}
              </span>
            </button>
          </div>
        </template>

        <!-- Real Day View matching CalendarApp::CreateDayView() -->
        <div v-else class="relative h-[202px] w-full">
          <div
            class="absolute top-[8px] left-[10px] flex size-[92px] flex-col items-center justify-center rounded-[10px]"
            :style="{ backgroundColor: t.bgSecondary }"
          >
            <div class="text-[28px] leading-[30px] font-semibold">{{ calDayViewCell.d }}</div>
            <div class="mt-[6px] text-[11.5px]" :style="{ color: t.textTertiary }">
              {{ relativeDayLabel(calDayViewCell) }}
            </div>
          </div>

          <div class="absolute top-[10px] left-[114px] w-[196px] space-y-[6px]">
            <div class="text-[14px] font-medium">丙午年 · 农历{{ calDayViewCell.l }}</div>
            <div class="text-[13px]" :style="{ color: t.textSecondary }">
              {{ calDayViewCell.wk ? '周末' : '工作日' }} · 第 48 周
            </div>
            <div v-if="calDayViewCell.term" class="text-[13px]" :style="{ color: t.secondary }">
              节气 / 节日：{{ calDayViewCell.l }}
            </div>
          </div>

          <div class="absolute top-[110px] left-[12px] text-[13px]" :style="{ color: t.textTertiary }">日程</div>
          <div
            class="absolute top-[134px] left-[10px] flex h-[56px] w-[300px] items-center justify-center rounded-[8px] text-[13px]"
            :style="{ border: `1px solid ${t.border}`, color: t.textTertiary }"
          >
            日程服务接入后显示在这里
          </div>
        </div>
      </template>

      <!-- ==================== GYRO APP ==================== -->
      <template v-else-if="scene === 'gyro'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">
            {{ gyroBarsMode ? '六轴实时条图' : '陀螺仪' }}
          </div>
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: gyroBarsMode ? t.primary : t.bgTertiary }"
            @click="gyroBarsMode = !gyroBarsMode"
          >
            <LandingRodakIonIcon
              name="hardware-chip-outline"
              :size="18"
              :color="gyroBarsMode ? t.onPrimary : t.textPrimary"
            />
          </button>
        </div>

        <div v-if="!gyroBarsMode" class="flex h-[202px] gap-[12px] px-[12px] pt-[10px] pb-[8px]">
          <div class="flex w-[152px] flex-col items-center justify-between">
            <svg
              width="150"
              height="150"
              viewBox="0 0 150 150"
              class="cursor-crosshair"
              @pointermove="handleDialPointer"
              @pointerdown="handleDialPointer"
            >
              <circle
                cx="75"
                cy="75"
                r="72"
                :fill="t.isLight ? '#F8F9FA' : '#14171A'"
                :stroke="isLevel ? t.success : t.isLight ? '#DCDFE4' : '#353A40'"
                stroke-width="2"
              />
              <circle
                cx="75"
                cy="75"
                r="48"
                fill="none"
                :stroke="t.isLight ? '#E5E8EC' : '#263038'"
                stroke-width="1.2"
                stroke-dasharray="4 4"
              />
              <line x1="14" y1="75" x2="136" y2="75" :stroke="t.isLight ? '#E2E5E9' : '#2B3036'" stroke-width="1" />
              <line x1="75" y1="14" x2="75" y2="136" :stroke="t.isLight ? '#E2E5E9' : '#2B3036'" stroke-width="1" />
              <circle cx="75" cy="75" r="18" fill="none" :stroke="t.isLight ? '#DCDFE4' : '#353A40'" stroke-width="1.6" />
              <line
                :x1="horizonLine.x1"
                :y1="horizonLine.y1"
                :x2="horizonLine.x2"
                :y2="horizonLine.y2"
                :stroke="t.isLight ? '#8EC5FA' : '#5E8CA8'"
                stroke-width="2"
                stroke-linecap="round"
              />
              <circle cx="75" cy="75" r="2.6" :fill="t.textTertiary" />
              <polygon points="118,32 108,26 119,21" :fill="t.secondary" transform="rotate(14 114 27)" />
              <circle :cx="bubblePos.x" :cy="bubblePos.y" r="14" :fill="isLevel ? t.success : t.primary" />
              <circle :cx="bubblePos.x" :cy="bubblePos.y" r="3.6" :fill="t.isLight ? '#FFFFFF' : '#000000'" />
            </svg>
            <div class="text-[11.5px] font-medium" :style="{ color: t.textTertiary }">
              {{ gyroFrozen ? '已定格 · 1.00 g' : 'ω 26.4 °/s · 1.02 g' }}
            </div>
          </div>

          <div class="flex flex-1 flex-col justify-between">
            <div class="flex items-center gap-[6px] pl-[2px] text-[13.5px] font-medium">
              <span
                class="inline-block size-[7px] rounded-full"
                :style="isLevel ? { backgroundColor: t.success } : { border: `1.5px solid ${t.textTertiary}` }"
              />
              <span :style="{ color: isLevel ? t.success : t.textPrimary }">
                {{ gyroFrozen ? '已定格' : isLevel ? '已水平' : '倾斜中' }}
              </span>
            </div>

            <div class="overflow-hidden rounded-[10px]" :style="{ backgroundColor: t.bgSecondary }">
              <div
                class="flex items-center justify-between px-[10px] py-[8px] text-[13px]"
                :style="{ borderBottom: `1px solid ${t.divider}` }"
              >
                <span :style="{ color: t.textSecondary }">横滚</span>
                <span class="font-semibold">{{ displayRoll >= 0 ? `+${displayRoll}°` : `${displayRoll}°` }}</span>
              </div>
              <div
                class="flex items-center justify-between px-[10px] py-[8px] text-[13px]"
                :style="{ borderBottom: `1px solid ${t.divider}` }"
              >
                <span :style="{ color: t.textSecondary }">俯仰</span>
                <span class="font-semibold">{{ displayPitch >= 0 ? `+${displayPitch}°` : `${displayPitch}°` }}</span>
              </div>
              <div class="flex items-center justify-between px-[10px] py-[8px] text-[13px]">
                <span :style="{ color: t.textSecondary }">偏航</span>
                <span class="font-semibold" :style="{ color: t.textSecondary }">
                  {{ gyroYaw >= 0 ? `+${gyroYaw}°` : `${gyroYaw}°` }}
                </span>
              </div>
            </div>

            <div class="grid grid-cols-2 gap-[8px]">
              <button
                type="button"
                class="flex h-[36px] cursor-pointer items-center justify-center rounded-[9px] text-[13.5px] font-medium active:scale-95"
                :style="{ backgroundColor: t.bgSecondary, color: t.textPrimary }"
                @click="zeroGyro"
              >
                归零
              </button>
              <button
                type="button"
                class="flex h-[36px] cursor-pointer items-center justify-center rounded-[9px] text-[13.5px] font-medium active:scale-95"
                :style="{
                  backgroundColor: gyroFrozen ? t.primary : t.bgSecondary,
                  color: gyroFrozen ? t.onPrimary : t.textPrimary
                }"
                @click="gyroFrozen = !gyroFrozen"
              >
                {{ gyroFrozen ? '继续' : '定格' }}
              </button>
            </div>
          </div>
        </div>

        <div v-else class="flex h-[202px] flex-col justify-between p-[10px]">
          <div class="rounded-[11px] p-[10px]" :style="{ backgroundColor: t.bgSecondary }">
            <div
              v-for="axis in [
                { lbl: 'Acc X', val: displayRoll / 45, text: `${(displayRoll * 0.017).toFixed(2)} g`, col: t.primary },
                { lbl: 'Acc Y', val: displayPitch / 45, text: `${(displayPitch * 0.017).toFixed(2)} g`, col: t.primary },
                { lbl: 'Acc Z', val: 0.72, text: '0.98 g', col: t.success },
                { lbl: 'Gyr X', val: 0.24, text: '+12.4 °/s', col: t.secondary },
                { lbl: 'Gyr Y', val: -0.18, text: '-8.6 °/s', col: t.secondary },
                { lbl: 'Gyr Z', val: 0.35, text: '+21.0 °/s', col: t.accent }
              ]"
              :key="axis.lbl"
              class="mb-[8px] flex items-center gap-[8px] last:mb-0"
            >
              <span class="w-[42px] text-[11px] font-medium" :style="{ color: t.textSecondary }">{{ axis.lbl }}</span>
              <div class="relative h-[8px] flex-1 overflow-hidden rounded-full" :style="{ backgroundColor: t.bgTertiary }">
                <div class="absolute top-0 bottom-0 left-1/2 w-[1px] bg-white/25" />
                <div
                  class="absolute top-0 bottom-0 rounded-full"
                  :style="{
                    backgroundColor: axis.col,
                    left: axis.val >= 0 ? '50%' : `${50 + Math.max(-50, axis.val * 50)}%`,
                    width: `${Math.min(50, Math.abs(axis.val) * 50)}%`
                  }"
                />
              </div>
              <span class="w-[62px] text-right text-[11px]" :style="{ color: t.textPrimary }">{{ axis.text }}</span>
            </div>
          </div>
          <div class="text-center text-[11px]" :style="{ color: t.textTertiary }">
            QMI8658 独立后台任务采样 · 零 LVGL 线程阻塞
          </div>
        </div>
      </template>

      <!-- ==================== SMART APP (Exact 6 presets + whole-hero toggle + off-color save) ==================== -->
      <template v-else-if="scene === 'smart'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="selectedLightId ? (selectedLightId = null) : goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">
            {{ selectedLight ? selectedLight.name : `智能家居 · ${lights.length}` }}
          </div>
          <div class="w-[38px]" />
        </div>

        <div v-if="!selectedLight" class="grid grid-cols-2 gap-[10px] p-[10px]">
          <div
            v-for="light in lights"
            :key="light.id"
            class="flex h-[86px] cursor-pointer flex-col justify-between rounded-[11px] px-[10px] py-[9px] transition-opacity hover:opacity-95"
            :style="{ backgroundColor: t.bgSecondary }"
            @click="selectedLightId = light.id"
          >
            <div class="flex items-center justify-between">
              <div
                class="flex size-[30px] items-center justify-center rounded-full"
                :style="{ backgroundColor: light.on ? light.color : t.bgTertiary }"
              >
                <LandingRodakIonIcon
                  name="bulb-outline"
                  :size="18"
                  :color="light.on ? '#1A1A1A' : t.textTertiary"
                />
              </div>
              <button
                type="button"
                class="flex h-[22px] w-[38px] cursor-pointer items-center rounded-full p-[2.5px] transition-colors"
                :style="{
                  backgroundColor: light.on ? t.primary : t.bgTertiary,
                  justifyContent: light.on ? 'flex-end' : 'flex-start'
                }"
                @click.stop="toggleLight(light.id)"
              >
                <span class="block size-[17px] rounded-full bg-white shadow-xs" />
              </button>
            </div>
            <div class="text-[13.5px] font-medium">{{ light.name }}</div>
            <div
              class="flex items-center gap-[5px] text-[11.5px]"
              :style="{ color: light.on ? t.textSecondary : t.textTertiary }"
            >
              <span
                class="inline-block size-[6.5px] rounded-full"
                :style="light.on ? { backgroundColor: t.success } : { border: `1.5px solid ${t.textTertiary}` }"
              />
              <span>{{ light.on ? `已开启 · ${light.brightness}%` : '已关闭' }}</span>
            </div>
          </div>
        </div>

        <!-- Single-Light Control View (exact smart_app.cc: tapping whole hero toggles power; picking color while OFF saves without turning on) -->
        <div v-else class="flex flex-col gap-[8px] px-[10px] py-[8px]">
          <button
            type="button"
            class="flex cursor-pointer items-center gap-[12px] rounded-[10px] px-[12px] py-[10px] text-left active:opacity-90"
            :style="{ backgroundColor: t.bgSecondary }"
            @click="toggleLight(selectedLight.id)"
          >
            <div
              class="flex size-[44px] shrink-0 items-center justify-center rounded-full"
              :style="{ backgroundColor: selectedLight.on ? selectedLight.color : t.bgTertiary }"
            >
              <LandingRodakIonIcon
                name="bulb-outline"
                :size="22"
                :color="selectedLight.on ? '#212121' : t.textTertiary"
              />
            </div>
            <div class="flex flex-1 flex-col gap-[3px]">
              <div class="text-[14px] font-semibold">{{ selectedLight.name }}</div>
              <div class="flex items-center gap-[5px] text-[12px]" :style="{ color: t.textSecondary }">
                <span
                  class="inline-block size-[7px] rounded-full"
                  :style="selectedLight.on ? { backgroundColor: t.success } : { border: `1px solid ${t.textTertiary}` }"
                />
                <span>{{ selectedLight.on ? `已开启 · ${selectedLight.brightness}%` : '已关闭' }}</span>
              </div>
            </div>
            <div
              class="flex h-[26px] w-[48px] items-center rounded-full p-[3px]"
              :style="{
                backgroundColor: selectedLight.on ? t.primary : t.bgTertiary,
                justifyContent: selectedLight.on ? 'flex-end' : 'flex-start'
              }"
            >
              <span class="block size-[20px] rounded-full bg-white" />
            </div>
          </button>

          <div class="rounded-[10px] px-[12px] pt-[6px] pb-[10px]" :style="{ backgroundColor: t.bgSecondary }">
            <div class="flex items-center justify-between text-[13px]">
              <span class="inline-flex items-center gap-[6px] font-medium" :style="{ color: t.textSecondary }">
                <LandingRodakIonIcon name="sunny-outline" :size="16" :color="t.primary" />
                亮度
              </span>
              <span class="text-[15px] font-semibold">{{ selectedLight.brightness }}%</span>
            </div>
            <input
              v-model.number="selectedLight.brightness"
              type="range"
              min="1"
              max="100"
              class="mt-[6px] h-[8px] w-full cursor-pointer accent-sky-400"
            >
          </div>

          <div class="rounded-[10px] px-[12px] pt-[6px] pb-[8px]" :style="{ backgroundColor: t.bgSecondary }">
            <div class="mb-[6px] flex items-center justify-between text-[13px]">
              <span :style="{ color: t.textSecondary }">颜色</span>
              <span class="text-[12.5px]" :style="{ color: t.textTertiary }">{{ selectedLight.colorName }}</span>
            </div>
            <div class="grid grid-cols-6 gap-[7px]">
              <button
                v-for="sw in SMART_COLOR_SWATCHES"
                :key="sw.color"
                type="button"
                class="flex h-[28px] cursor-pointer items-center justify-center rounded-[8px] active:scale-95"
                :style="{
                  backgroundColor: sw.color,
                  boxShadow: selectedLight.color === sw.color ? 'inset 0 0 0 2px #111827' : undefined
                }"
                @click="setLightColor(selectedLight.id, sw.color, sw.name)"
              >
                <LandingRodakIonIcon
                  v-if="selectedLight.color === sw.color"
                  name="checkmark-outline"
                  :size="16"
                  color="#111827"
                />
              </button>
            </div>
          </div>
        </div>
      </template>

      <!-- ==================== THEMES & PUBLISHER TRUST ==================== -->
      <template v-else-if="scene === 'themes' || scene === 'themes-trust'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="scene === 'themes-trust' ? openScene('themes') : goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">
            {{ scene === 'themes-trust' ? '发布端信任' : '主题' }}
          </div>
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="home-outline" :size="17" :color="t.textPrimary" />
          </button>
        </div>

        <div v-if="scene === 'themes'" class="flex flex-col gap-[8px] px-[10px] py-[8px]">
          <div class="flex items-center gap-[14px] rounded-[11px] p-[10px]" :style="{ backgroundColor: t.bgSecondary }">
            <div
              class="flex h-[78px] w-[112px] shrink-0 flex-col justify-between overflow-hidden rounded-[8px]"
              :style="{ backgroundColor: t.bgPrimary, border: `1px solid ${t.border}` }"
            >
              <div class="h-[14px] w-full" :style="{ backgroundColor: t.bgSecondary }" />
              <div class="flex justify-center gap-[10px] pt-[2px]">
                <span class="size-[20px] rounded-[6px]" :style="{ backgroundColor: t.slots.system }" />
                <span class="size-[20px] rounded-[6px]" :style="{ backgroundColor: t.slots.media }" />
                <span class="size-[20px] rounded-[6px]" :style="{ backgroundColor: t.slots.tools }" />
              </div>
              <div class="flex items-center gap-[10px] px-[12px] pb-[10px]">
                <span class="h-[13px] w-[46px] rounded-full" :style="{ backgroundColor: t.primary }" />
                <span class="text-[12px] font-semibold">Aa</span>
              </div>
            </div>
            <div class="flex flex-col gap-[4px]">
              <div class="text-[14.5px] font-medium">配色：本地</div>
              <div class="text-[13px]" :style="{ color: t.textSecondary }">内置外观</div>
              <div class="text-[13px]" :style="{ color: t.success }">已生效</div>
            </div>
          </div>

          <div
            class="flex items-center justify-between rounded-[11px] px-[12px] py-[9px]"
            :style="{ backgroundColor: t.bgSecondary }"
          >
            <span class="inline-flex items-center gap-[8px] text-[13.5px] font-medium">
              <LandingRodakIonIcon name="color-palette-outline" :size="18" :color="t.primary" />
              配色
            </span>
            <div class="flex items-center gap-[7px]">
              <button
                v-for="p in [
                  { id: 'light' as FirmwareThemeId, key: 'L', bg: '#FFFFFF', fg: '#212121' },
                  { id: 'dark' as FirmwareThemeId, key: 'D', bg: '#0B0B0B', fg: '#F4F4F5' },
                  { id: 'blue' as FirmwareThemeId, key: 'B', bg: '#1976D2', fg: '#FFFFFF' },
                  { id: 'green' as FirmwareThemeId, key: 'G', bg: '#388E3C', fg: '#FFFFFF' }
                ]"
                :key="p.id"
                type="button"
                class="flex h-[24px] w-[40px] cursor-pointer items-center justify-center rounded-[7px] text-[12px] font-semibold active:scale-95"
                :style="{
                  backgroundColor: p.bg,
                  color: p.fg,
                  border:
                    theme === p.id
                      ? `2px solid ${t.isLight ? '#212121' : '#FFFFFF'}`
                      : `1px solid ${t.isLight ? '#D4D4D8' : '#3F3F46'}`
                }"
                @click="emit('update:theme', p.id)"
              >
                {{ p.key }}
              </button>
            </div>
          </div>

          <button
            type="button"
            class="flex cursor-pointer items-center justify-between rounded-[11px] px-[12px] py-[9px] text-left active:scale-[0.99]"
            :style="{ backgroundColor: t.bgSecondary }"
            @click="openScene('themes-trust')"
          >
            <span class="inline-flex items-center gap-[8px] text-[13.5px] font-medium">
              <LandingRodakIonIcon
                name="lock-closed-outline"
                :size="17"
                :color="publisherTrusted ? t.success : t.primary"
              />
              <span>发布端：{{ publisherTrusted ? '已信任' : '未信任' }}</span>
            </span>
            <LandingRodakIonIcon name="chevron-forward-outline" :size="16" :color="t.textTertiary" />
          </button>
        </div>

        <div v-else class="flex h-[202px] flex-col justify-between p-[10px]">
          <div class="flex flex-col gap-[7px] rounded-[12px] px-[12px] py-[11px]" :style="{ backgroundColor: t.bgSecondary }">
            <p class="text-[12px] leading-[16.5px]" :style="{ color: t.textSecondary }">
              在 Rodak 对照指纹，一致后退出远程控制并在本机触摸确认。
            </p>
            <div class="flex items-center justify-between text-[12px]">
              <span :style="{ color: t.textTertiary }">发布端指纹</span>
              <span :style="{ color: publisherTrusted ? t.success : t.textSecondary }">
                {{ publisherTrusted ? '已物理确认信任' : '尚未信任' }}
              </span>
            </div>
            <div class="font-mono text-[13px] leading-[18.5px] tracking-[0.5px]">
              <div>A3C1 F0E2 B4D6 A8C0</div>
              <div>E2F4 A6B8 C0D2 E4F6</div>
            </div>
          </div>
          <div class="grid grid-cols-[96px_1fr] gap-[8px]">
            <button
              type="button"
              class="flex h-[38px] cursor-pointer items-center justify-center rounded-[10px] text-[13px] font-medium active:scale-95"
              :style="{ backgroundColor: t.bgTertiary, color: t.textPrimary }"
              @click="publisherTrusted = false"
            >
              {{ publisherTrusted ? '撤销信任' : '读取/刷新' }}
            </button>
            <button
              type="button"
              class="flex h-[38px] cursor-pointer items-center justify-center rounded-[10px] text-[13px] font-semibold active:scale-95"
              :style="{
                backgroundColor: publisherTrusted ? t.success : t.primary,
                color: t.onPrimary
              }"
              @click="publisherTrusted = true"
            >
              {{ publisherTrusted ? '已确认信任 ✓' : '指纹一致，确认信任' }}
            </button>
          </div>
        </div>
      </template>

      <!-- ==================== ASSISTANT APP ==================== -->
      <template v-else-if="scene === 'assistant'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">语音助手</div>
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="openScene('system')"
          >
            <LandingRodakIonIcon name="information-circle-outline" :size="18" :color="t.textPrimary" />
          </button>
        </div>

        <div class="flex flex-col gap-[9px] px-[10px] py-[9px]">
          <button
            type="button"
            class="flex cursor-pointer items-center gap-[14px] rounded-[12px] px-[14px] py-[12px] text-left active:scale-[0.99]"
            :style="{ backgroundColor: t.bgSecondary }"
            @click="triggerAssistantDemo"
          >
            <div
              class="flex size-[44px] shrink-0 items-center justify-center rounded-[12px] transition-colors"
              :style="{ backgroundColor: assistantState === 'thinking' ? t.primary : '#314354' }"
            >
              <LandingRodakIonIcon
                name="chatbubble-ellipses-outline"
                :size="22"
                :color="assistantState === 'thinking' ? '#0A1622' : t.primary"
              />
            </div>
            <div class="flex flex-col gap-[4px]">
              <div
                class="flex items-center gap-[6px] text-[14.5px] font-medium"
                :style="{ color: assistantState === 'thinking' ? t.primary : t.textPrimary }"
              >
                <span
                  class="inline-block size-[7px] rounded-full"
                  :style="{ backgroundColor: assistantState === 'thinking' ? t.primary : t.success }"
                />
                <span>
                  {{
                    assistantState === 'thinking'
                      ? '正在思考'
                      : assistantState === 'speaking'
                        ? '正在播报'
                        : '待命中（点击模拟唤醒）'
                  }}
                </span>
              </div>
              <div class="text-[12.5px]" :style="{ color: t.textSecondary }">
                {{ assistantState === 'thinking' ? '等待罗达克回复...' : '说「你好达克」开始对话' }}
              </div>
            </div>
          </button>

          <div class="overflow-hidden rounded-[12px]" :style="{ backgroundColor: t.bgSecondary }">
            <div
              class="flex h-[38px] items-center justify-between px-[12px]"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
            >
              <span class="inline-flex items-center gap-[9px] text-[13.5px] font-medium">
                <LandingRodakIonIcon name="mic-outline" :size="17" :color="t.primary" />
                语音唤醒
              </span>
              <button
                type="button"
                class="flex h-[22px] w-[40px] cursor-pointer items-center rounded-full p-[2.5px]"
                :style="{
                  backgroundColor: wakeEnabled ? t.primary : t.bgTertiary,
                  justifyContent: wakeEnabled ? 'flex-end' : 'flex-start'
                }"
                @click="wakeEnabled = !wakeEnabled"
              >
                <span class="block size-[17px] rounded-full bg-white" />
              </button>
            </div>
            <div
              class="flex h-[38px] items-center justify-between px-[12px]"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
            >
              <span class="inline-flex items-center gap-[9px] text-[13.5px] font-medium">
                <LandingRodakIonIcon name="bulb-outline" :size="17" :color="t.primary" />
                语音状态灯
              </span>
              <div class="flex items-center gap-[7px]">
                <span class="text-[12px]" :style="{ color: t.textTertiary }">30%</span>
                <button
                  type="button"
                  class="flex h-[22px] w-[40px] cursor-pointer items-center rounded-full p-[2.5px]"
                  :style="{
                    backgroundColor: voiceLedEnabled ? t.primary : t.bgTertiary,
                    justifyContent: voiceLedEnabled ? 'flex-end' : 'flex-start'
                  }"
                  @click="voiceLedEnabled = !voiceLedEnabled"
                >
                  <span class="block size-[17px] rounded-full bg-white" />
                </button>
              </div>
            </div>
            <div class="flex h-[38px] items-center justify-between px-[12px]">
              <span class="inline-flex items-center gap-[9px] text-[13.5px] font-medium">
                <LandingRodakIonIcon name="cloud-outline" :size="17" :color="t.primary" />
                连接服务
              </span>
              <div class="flex items-center gap-[6px]">
                <span class="inline-block size-[7px] rounded-full" :style="{ backgroundColor: t.success }" />
                <span class="text-[13px] font-medium" :style="{ color: t.success }">已连接</span>
                <LandingRodakIonIcon name="chevron-forward-outline" :size="14" :color="t.textTertiary" />
              </div>
            </div>
          </div>
        </div>
      </template>

      <!-- ==================== RECORDER APP ==================== -->
      <template v-else-if="scene === 'recorder'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">录音</div>
          <div class="z-10 flex items-center gap-[6px]">
            <LandingRodakIonIcon name="mic-off-outline" :size="17" :color="t.textTertiary" />
            <button
              type="button"
              class="flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px]"
              :style="{ backgroundColor: t.bgTertiary }"
              @click="openScene('files')"
            >
              <LandingRodakIonIcon name="list-outline" :size="18" :color="t.textPrimary" />
            </button>
          </div>
        </div>

        <div class="flex h-[202px] flex-col items-center justify-between px-[12px] pt-[10px] pb-[12px]">
          <div
            class="inline-flex items-center gap-[6px] text-[13px] font-medium"
            :style="{ color: isRecording ? t.accent : t.success }"
          >
            <span
              class="inline-block size-[7px] rounded-full"
              :class="isRecording ? 'animate-pulse' : ''"
              :style="{ backgroundColor: isRecording ? t.accent : t.success }"
            />
            <span>{{ isRecording ? `正在录音 · ${recSeconds * 32} KB` : recNotice || '就绪 · 点击红键开始' }}</span>
          </div>

          <div class="text-[24px] leading-[26px] font-semibold" :style="{ color: isRecording ? t.accent : t.textPrimary }">
            {{ recTimeLabel }}
          </div>

          <div
            class="flex h-[46px] w-full items-center justify-between rounded-[11px] px-[14px]"
            :style="{ backgroundColor: t.bgSecondary }"
          >
            <span
              v-for="(h, i) in meterLevels"
              :key="i"
              class="w-[7px] rounded-full transition-all duration-150"
              :style="{ height: `${h}px`, backgroundColor: h > 5 ? t.secondary : t.isLight ? '#D4D4D8' : '#2F3338' }"
            />
          </div>

          <div class="flex w-full items-center justify-between px-[12px]">
            <button
              type="button"
              class="inline-flex h-[36px] w-[78px] cursor-pointer items-center justify-center gap-[5px] rounded-[10px] text-[13px] active:scale-95"
              :style="{ backgroundColor: t.bgSecondary, color: t.textSecondary }"
              @click="recNotice = 'ES8311 1kHz 试音通过'"
            >
              <LandingRodakIonIcon name="volume-high-outline" :size="16" :color="t.textSecondary" />
              试音
            </button>

            <button
              type="button"
              class="flex size-[52px] cursor-pointer items-center justify-center rounded-full active:scale-95"
              :style="{ border: `2.5px solid ${t.accent}` }"
              @click="toggleRecording"
            >
              <span
                class="block transition-all"
                :style="{
                  width: isRecording ? '20px' : '34px',
                  height: isRecording ? '20px' : '34px',
                  borderRadius: isRecording ? '5px' : '999px',
                  backgroundColor: t.accent
                }"
              />
            </button>

            <button
              type="button"
              class="inline-flex h-[36px] w-[78px] cursor-pointer items-center justify-center gap-[5px] rounded-[10px] text-[13px] active:scale-95"
              :style="{ backgroundColor: t.bgSecondary, color: t.textSecondary }"
              @click="recNotice = '正在回放 REC_0004.WAV'"
            >
              <LandingRodakIonIcon name="play-outline" :size="15" :color="t.textSecondary" />
              回放
            </button>
          </div>
        </div>
      </template>

      <!-- ==================== WAKE-ON-LAN APP ==================== -->
      <template v-else-if="scene === 'wol'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">网络唤醒</div>
          <button
            type="button"
            class="z-10 flex h-[26px] w-[38px] cursor-pointer items-center justify-center rounded-[7px] active:scale-95"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="addDemoWolHost"
          >
            <LandingRodakIonIcon name="add-outline" :size="18" :color="t.textPrimary" />
          </button>
        </div>

        <div
          class="flex h-[24px] items-center justify-between px-[12px] text-[12px]"
          :style="{ borderBottom: `1px solid ${t.divider}` }"
        >
          <span class="inline-flex items-center gap-[6px] font-medium" :style="{ color: t.textSecondary }">
            <span class="inline-block size-[6.5px] rounded-full" :style="{ backgroundColor: t.success }" />
            书房-5G
          </span>
          <span :style="{ color: t.textTertiary }">{{ wolHosts.length }} / 8</span>
        </div>

        <div class="flex flex-col gap-[8px] px-[10px] py-[9px]">
          <div
            v-for="host in wolHosts"
            :key="host.id"
            class="flex items-center gap-[10px] rounded-[11px] py-[8px] pr-[10px] pl-[12px]"
            :style="{ backgroundColor: t.bgSecondary }"
          >
            <div class="flex flex-1 flex-col gap-[3px]">
              <div class="text-[14px] font-medium">{{ host.name }}</div>
              <div class="text-[11px]" :style="{ color: t.textTertiary }">{{ host.mac }}</div>
            </div>
            <div class="h-[32px] w-[1px] opacity-60" :style="{ backgroundColor: t.border }" />
            <button
              type="button"
              class="flex h-[38px] w-[44px] shrink-0 cursor-pointer items-center justify-center rounded-[9px] transition-transform active:scale-90"
              :style="{ backgroundColor: host.sent ? t.success : t.primary }"
              @click="sendWol(host.id)"
            >
              <LandingRodakIonIcon
                :name="host.sent ? 'checkmark-outline' : 'power-outline'"
                :size="20"
                :color="t.onPrimary"
              />
            </button>
          </div>
        </div>
      </template>

      <!-- ==================== CLOCK APP (Exact clock_app.cc layout) ==================== -->
      <template v-else-if="scene === 'clock'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">Clock</div>
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="home-outline" :size="17" :color="t.textPrimary" />
          </button>
        </div>

        <div class="relative h-[202px] w-full">
          <!-- Face card: 300x92 at (10, 8) -->
          <div
            class="absolute top-[8px] left-[10px] flex h-[90px] w-[300px] flex-col justify-between rounded-[8px] px-[16px] py-[12px]"
            :style="{ backgroundColor: t.bgSecondary }"
          >
            <div class="flex items-baseline gap-[4px]">
              <span class="text-[32px] leading-[34px] font-semibold">{{ clockNow }}</span>
              <span class="text-[18px] font-medium" :style="{ color: t.primary }">:{{ clockSec }}</span>
            </div>
            <div class="text-[12px]" :style="{ color: t.textSecondary }">Thu, 2026-11-26</div>
          </div>

          <!-- Time zone card -->
          <div
            class="absolute top-[106px] left-[10px] flex h-[34px] w-[300px] items-center gap-[10px] rounded-[8px] px-[10px]"
            :style="{ backgroundColor: t.bgSecondary }"
          >
            <LandingRodakIonIcon name="globe-outline" :size="16" :color="t.primary" />
            <span class="truncate text-[12px]">Time zone: Asia/Shanghai (UTC+8)</span>
          </div>

          <!-- Server card + Sync button -->
          <div
            class="absolute top-[146px] left-[10px] flex h-[34px] w-[300px] items-center justify-between rounded-[8px] px-[10px]"
            :style="{ backgroundColor: t.bgSecondary }"
          >
            <span class="inline-flex min-w-0 items-center gap-[10px] text-[12px]">
              <LandingRodakIonIcon name="cloud-outline" :size="16" :color="t.primary" />
              <span class="truncate">Server: pool.ntp.org</span>
            </span>
            <button
              type="button"
              class="flex h-[26px] w-[40px] shrink-0 cursor-pointer items-center justify-center rounded-[6px] active:scale-95"
              :style="{ backgroundColor: t.primary }"
              @click="triggerClockSync"
            >
              <LandingRodakIonIcon name="refresh-outline" :size="16" :color="t.onPrimary" />
            </button>
          </div>

          <div
            class="absolute right-0 bottom-[3px] left-0 text-center text-[11px]"
            :style="{ color: sntpStatus === 'Synced' ? t.success : t.primary }"
          >
            {{ sntpStatus }}
          </div>
        </div>
      </template>

      <!-- ==================== SETTINGS APP (Exact settings_main_page.cc + 按键 + 锁屏与控制中心) ==================== -->
      <template v-else-if="scene === 'settings'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="settingsSubPage === 'main' ? goHome(false) : (settingsSubPage = 'main')"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">
            {{
              settingsSubPage === 'buttons'
                ? '按键'
                : settingsSubPage === 'shell'
                  ? '锁屏与控制中心'
                  : '设置'
            }}
          </div>
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="home-outline" :size="17" :color="t.textPrimary" />
          </button>
        </div>

        <!-- Settings Main Scrollable List -->
        <div v-if="settingsSubPage === 'main'" class="h-[202px] space-y-[6px] overflow-y-auto px-[10px] py-[4px]">
          <div class="px-[4px] text-[11px]" :style="{ color: t.textTertiary }">网络</div>
          <div class="overflow-hidden rounded-[9px]" :style="{ backgroundColor: t.bgSecondary }">
            <div
              class="flex h-[36px] items-center justify-between px-[10px]"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
            >
              <span class="inline-flex items-center gap-[8px] text-[13px]">
                <LandingRodakIonIcon name="wifi-outline" :size="15" :color="t.primary" /> Wi-Fi
              </span>
              <span class="text-[12px]" :style="{ color: t.textSecondary }">书房-5G</span>
            </div>
            <div class="flex h-[36px] items-center justify-between px-[10px]">
              <span class="inline-flex items-center gap-[8px] text-[13px]">
                <LandingRodakIonIcon name="link-outline" :size="15" :color="t.primary" /> 连接服务
              </span>
              <span class="text-[12px]" :style="{ color: t.success }">已连接</span>
            </div>
          </div>

          <div class="px-[4px] pt-[2px] text-[11px]" :style="{ color: t.textTertiary }">显示</div>
          <div class="overflow-hidden rounded-[9px]" :style="{ backgroundColor: t.bgSecondary }">
            <div
              class="flex h-[36px] items-center justify-between gap-[8px] px-[10px]"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
            >
              <LandingRodakIonIcon name="sunny-outline" :size="15" :color="t.primary" />
              <input v-model.number="brightness" type="range" min="5" max="100" class="flex-1 cursor-pointer accent-sky-400">
              <span class="w-[34px] text-right text-[11.5px]" :style="{ color: t.textSecondary }">{{ brightness }}%</span>
            </div>
            <button
              type="button"
              class="flex h-[36px] w-full cursor-pointer items-center justify-between px-[10px] text-left"
              @click="openScene('themes')"
            >
              <span class="inline-flex items-center gap-[8px] text-[13px]">
                <LandingRodakIonIcon name="color-palette-outline" :size="15" :color="t.primary" /> 主题
              </span>
              <LandingRodakIonIcon name="chevron-forward-outline" :size="14" :color="t.textTertiary" />
            </button>
          </div>

          <div class="px-[4px] pt-[2px] text-[11px]" :style="{ color: t.textTertiary }">设备</div>
          <div class="overflow-hidden rounded-[9px]" :style="{ backgroundColor: t.bgSecondary }">
            <button
              type="button"
              class="flex h-[36px] w-full cursor-pointer items-center justify-between px-[10px] text-left"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
              @click="settingsSubPage = 'buttons'"
            >
              <span class="inline-flex items-center gap-[8px] text-[13px]">
                <LandingRodakIonIcon name="key-outline" :size="15" :color="t.primary" /> 按键
              </span>
              <LandingRodakIonIcon name="chevron-forward-outline" :size="14" :color="t.textTertiary" />
            </button>
            <button
              type="button"
              class="flex h-[36px] w-full cursor-pointer items-center justify-between px-[10px] text-left"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
              @click="settingsSubPage = 'shell'"
            >
              <span class="inline-flex items-center gap-[8px] text-[13px]">
                <LandingRodakIonIcon name="lock-closed-outline" :size="15" :color="t.primary" /> 锁屏与控制中心
              </span>
              <LandingRodakIonIcon name="chevron-forward-outline" :size="14" :color="t.textTertiary" />
            </button>
            <button
              type="button"
              class="flex h-[36px] w-full cursor-pointer items-center justify-between px-[10px] text-left"
              @click="openScene('system')"
            >
              <span class="inline-flex items-center gap-[8px] text-[13px]">
                <LandingRodakIonIcon name="information-circle-outline" :size="15" :color="t.primary" /> 关于本机
              </span>
              <span class="inline-flex items-center gap-[4px] text-[12px]" :style="{ color: t.textTertiary }">
                v0.1.6
                <LandingRodakIonIcon name="chevron-forward-outline" :size="14" :color="t.textTertiary" />
              </span>
            </button>
          </div>
        </div>

        <!-- Settings -> 按键 (Exact button_binding_service.cc defaults) -->
        <div v-else-if="settingsSubPage === 'buttons'" class="h-[202px] space-y-[6px] overflow-y-auto px-[10px] py-[6px]">
          <div class="px-[4px] text-[11px]" :style="{ color: t.textTertiary }">BOOT 键 (IO0)</div>
          <div class="overflow-hidden rounded-[9px]" :style="{ backgroundColor: t.bgSecondary }">
            <div
              class="flex h-[34px] items-center justify-between px-[10px] text-[12.5px]"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
            >
              <span>单击</span>
              <span :style="{ color: t.primary }">回到桌面</span>
            </div>
            <div class="flex h-[34px] items-center justify-between px-[10px] text-[12.5px]">
              <span>双击 / 长按</span>
              <span :style="{ color: t.textTertiary }">不执行</span>
            </div>
          </div>

          <div class="px-[4px] pt-[2px] text-[11px]" :style="{ color: t.textTertiary }">IO10 键</div>
          <div class="overflow-hidden rounded-[9px]" :style="{ backgroundColor: t.bgSecondary }">
            <div
              class="flex h-[34px] items-center justify-between px-[10px] text-[12.5px]"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
            >
              <span>单击</span>
              <span :style="{ color: t.primary }">控制中心</span>
            </div>
            <div
              class="flex h-[34px] items-center justify-between px-[10px] text-[12.5px]"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
            >
              <span>双击</span>
              <span :style="{ color: t.primary }">打开 Smart</span>
            </div>
            <div class="flex h-[34px] items-center justify-between px-[10px] text-[12.5px]">
              <span>长按</span>
              <span :style="{ color: t.primary }">锁屏</span>
            </div>
          </div>
        </div>

        <!-- Settings -> 锁屏与控制中心 (Exact settings_system_shell_page.cc) -->
        <div v-else class="p-[10px]">
          <div class="overflow-hidden rounded-[9px]" :style="{ backgroundColor: t.bgSecondary }">
            <button
              type="button"
              class="flex w-full cursor-pointer items-center justify-between gap-[8px] px-[12px] py-[10px] text-left"
              :style="{ borderBottom: `1px solid ${t.divider}` }"
              @click="lockOnBoot = !lockOnBoot"
            >
              <div class="min-w-0">
                <div class="text-[13px] font-medium">开机后锁屏</div>
                <div class="mt-[2px] text-[11px]" :style="{ color: t.textTertiary }">开机后先显示锁屏，上滑解锁</div>
              </div>
              <span
                class="flex h-[22px] w-[40px] shrink-0 items-center rounded-full p-[2.5px]"
                :style="{
                  backgroundColor: lockOnBoot ? t.primary : t.bgTertiary,
                  justifyContent: lockOnBoot ? 'flex-end' : 'flex-start'
                }"
              >
                <span class="block size-[17px] rounded-full bg-white" />
              </span>
            </button>
            <button
              type="button"
              class="flex w-full cursor-pointer items-center justify-between gap-[8px] px-[12px] py-[10px] text-left"
              @click="controlCenterGestureEnabled = !controlCenterGestureEnabled"
            >
              <div class="min-w-0">
                <div class="text-[13px] font-medium">下滑打开控制中心</div>
                <div class="mt-[2px] text-[11px]" :style="{ color: t.textTertiary }">
                  从屏幕顶部向下滑。关闭后仍可用按键打开
                </div>
              </div>
              <span
                class="flex h-[22px] w-[40px] shrink-0 items-center rounded-full p-[2.5px]"
                :style="{
                  backgroundColor: controlCenterGestureEnabled ? t.primary : t.bgTertiary,
                  justifyContent: controlCenterGestureEnabled ? 'flex-end' : 'flex-start'
                }"
              >
                <span class="block size-[17px] rounded-full bg-white" />
              </span>
            </button>
          </div>
        </div>
      </template>

      <!-- ==================== SYSTEM INFO APP (关于本机) ==================== -->
      <template v-else-if="scene === 'system'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">关于本机</div>
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="home-outline" :size="17" :color="t.textPrimary" />
          </button>
        </div>
        <div class="space-y-[8px] p-[10px]">
          <div class="flex items-center justify-between rounded-[10px] px-[12px] py-[10px]" :style="{ backgroundColor: t.bgSecondary }">
            <div>
              <div class="text-[14px] font-semibold">RYMCU BigSmart</div>
              <div class="mt-[2px] text-[11.5px]" :style="{ color: t.textTertiary }">RodakOS v0.1.6 · ESP-IDF 6.0.2</div>
            </div>
            <span class="rounded-full px-[8px] py-[2px] text-[11px] font-medium" :style="{ backgroundColor: t.bgTertiary, color: t.success }">
              Recovery OK
            </span>
          </div>
          <div class="overflow-hidden rounded-[10px]" :style="{ backgroundColor: t.bgSecondary }">
            <div
              v-for="([k, v], i) in [
                ['内部 SRAM', '312 KB 可用 (DMA 48 KB)'],
                ['外部 PSRAM', '6.4 MB / 8.0 MB'],
                ['Flash 主槽', 'ota_0 13.3 MB · SD OTA'],
                ['Wi-Fi', '书房-5G · -54 dBm']
              ]"
              :key="k"
              class="flex h-[32px] items-center justify-between px-[12px] text-[12px]"
              :style="i < 3 ? { borderBottom: `1px solid ${t.divider}` } : undefined"
            >
              <span :style="{ color: t.textTertiary }">{{ k }}</span>
              <span class="font-medium">{{ v }}</span>
            </div>
          </div>
        </div>
      </template>

      <!-- ==================== MUSIC APP (Exact music_app.cc geometry) ==================== -->
      <template v-else-if="scene === 'music'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">音乐</div>
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="openScene('files')"
          >
            <LandingRodakIonIcon name="list-outline" :size="18" :color="t.textPrimary" />
          </button>
        </div>

        <div class="relative h-[202px] w-full">
          <!-- Cover 96x96 at (16, 12) -->
          <div
            class="absolute top-[12px] left-[16px] flex size-[96px] items-center justify-center rounded-[12px]"
            :style="{ background: tileGradient('media') }"
          >
            <LandingRodakIonIcon name="musical-notes-outline" :size="38" color="#FFFFFF" />
          </div>

          <!-- Info + 3 chips at x=124 -->
          <div class="absolute top-[14px] left-[124px] w-[180px]">
            <div class="truncate text-[15px] font-semibold">{{ MUSIC_TRACKS[musicTrackIdx]?.title }}</div>
            <div class="mt-[3px] truncate text-[11.5px]" :style="{ color: t.textTertiary }">
              {{ MUSIC_TRACKS[musicTrackIdx]?.artist }}
            </div>
            <div class="mt-[14px] grid grid-cols-3 gap-[6px]">
              <button
                type="button"
                class="flex h-[32px] cursor-pointer items-center justify-center rounded-[8px] text-[11px]"
                :style="{ backgroundColor: t.bgSecondary, color: t.textSecondary }"
                @click="musicMode = musicMode === '顺序' ? '单曲' : '顺序'"
              >
                {{ musicMode }}
              </button>
              <div
                class="flex h-[32px] items-center justify-center rounded-[8px] text-[11px]"
                :style="{ backgroundColor: t.bgSecondary, color: t.textSecondary }"
              >
                70%
              </div>
              <div
                class="flex h-[32px] items-center justify-center rounded-[8px] text-[11px]"
                :style="{ backgroundColor: t.bgSecondary, color: t.textSecondary }"
              >
                {{ musicTrackIdx + 1 }}/2
              </div>
            </div>
          </div>

          <!-- Progress bar y=118 -->
          <div class="absolute top-[118px] right-[16px] left-[16px]">
            <div class="h-[5px] overflow-hidden rounded-full" :style="{ backgroundColor: t.bgTertiary }">
              <div class="h-full rounded-full transition-all" :style="{ width: `${musicProgress}%`, backgroundColor: t.primary }" />
            </div>
            <div class="mt-[3px] flex justify-between text-[10.5px]" :style="{ color: t.textTertiary }">
              <span>1:18</span>
              <span>{{ MUSIC_TRACKS[musicTrackIdx]?.dur }}</span>
            </div>
          </div>

          <!-- Transport bar -->
          <div class="absolute right-0 bottom-[6px] left-0 flex items-center justify-center gap-[22px]">
            <button
              type="button"
              class="flex size-[40px] cursor-pointer items-center justify-center rounded-full active:scale-95"
              :style="{ backgroundColor: t.bgSecondary }"
              @click="musicTrackIdx = (musicTrackIdx + 1) % MUSIC_TRACKS.length"
            >
              <LandingRodakIonIcon name="play-skip-back-outline" :size="18" :color="t.textPrimary" />
            </button>
            <button
              type="button"
              class="flex size-[48px] cursor-pointer items-center justify-center rounded-full active:scale-95"
              :style="{ backgroundColor: t.primary }"
              @click="musicPlaying = !musicPlaying"
            >
              <LandingRodakIonIcon :name="musicPlaying ? 'pause-outline' : 'play-outline'" :size="22" :color="t.onPrimary" />
            </button>
            <button
              type="button"
              class="flex size-[40px] cursor-pointer items-center justify-center rounded-full active:scale-95"
              :style="{ backgroundColor: t.bgSecondary }"
              @click="musicTrackIdx = (musicTrackIdx + 1) % MUSIC_TRACKS.length"
            >
              <LandingRodakIonIcon name="play-skip-forward-outline" :size="18" :color="t.textPrimary" />
            </button>
          </div>
        </div>
      </template>

      <!-- ==================== FILES APP ==================== -->
      <template v-else-if="scene === 'files'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">文件</div>
          <div class="w-[40px]" />
        </div>
        <div
          class="flex h-[24px] items-center justify-between px-[12px] text-[11.5px]"
          :style="{ color: t.textTertiary, borderBottom: `1px solid ${t.divider}` }"
        >
          <span>{{ selectedFileNote || '/ · SDMMC FAT32' }}</span>
          <span>1.4 / 32 GB</span>
        </div>
        <div class="px-[10px] py-[8px]">
          <div class="overflow-hidden rounded-[10px]" :style="{ backgroundColor: t.bgSecondary }">
            <button
              v-for="(d, i) in [
                { name: '/photos', meta: '8 张图片', icon: 'folder-outline' as RodakosIonName, col: t.primary, target: 'photos' as ScreenSceneId },
                { name: '/music', meta: '2 首音频', icon: 'folder-outline' as RodakosIonName, col: t.primary, target: 'music' as ScreenSceneId },
                { name: '/recordings', meta: '4 条录音', icon: 'folder-outline' as RodakosIonName, col: t.primary, target: 'recorder' as ScreenSceneId },
                { name: '/rodakos', meta: '受保护系统目录', icon: 'lock-closed-outline' as RodakosIonName, col: t.textTertiary, target: null }
              ]"
              :key="d.name"
              type="button"
              class="flex h-[39px] w-full cursor-pointer items-center justify-between px-[12px] text-left"
              :style="i < 3 ? { borderBottom: `1px solid ${t.divider}` } : undefined"
              @click="d.target ? openScene(d.target) : (selectedFileNote = '/rodakos 系统目录不可删除')"
            >
              <span class="inline-flex items-center gap-[9px] text-[13px] font-medium">
                <LandingRodakIonIcon :name="d.icon" :size="18" :color="d.col" />
                {{ d.name }}
              </span>
              <span class="text-[11.5px]" :style="{ color: t.textTertiary }">{{ d.meta }}</span>
            </button>
          </div>
        </div>
      </template>

      <!-- ==================== PHOTOS APP (Exact 4x2 tile grid + 40px pager bar) ==================== -->
      <template v-else-if="scene === 'photos'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="selectedPhotoIdx !== null ? (selectedPhotoIdx = null) : goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">
            {{ selectedPhotoIdx !== null ? DEMO_PHOTOS[selectedPhotoIdx]?.label : '相册' }}
          </div>
          <div class="w-[40px]" />
        </div>

        <template v-if="selectedPhotoIdx === null">
          <div class="grid grid-cols-4 gap-[2px] p-[2px]">
            <button
              v-for="(ph, idx) in DEMO_PHOTOS"
              :key="ph.label"
              type="button"
              class="flex h-[78px] cursor-pointer flex-col items-center justify-center rounded-[4px] active:scale-95"
              :style="{ background: ph.bg }"
              @click="selectedPhotoIdx = idx"
            >
              <LandingRodakIonIcon name="image-outline" :size="20" color="rgba(255,255,255,0.78)" />
            </button>
          </div>
          <div
            class="flex h-[40px] items-center justify-between px-[14px] text-[12px]"
            :style="{ backgroundColor: t.bgSecondary, color: t.textSecondary }"
          >
            <span class="opacity-40">上一页</span>
            <span>第 1 / 1 页 · 8 张</span>
            <span class="opacity-40">下一页</span>
          </div>
        </template>

        <div v-else class="relative h-[202px] w-full" :style="{ background: DEMO_PHOTOS[selectedPhotoIdx]?.bg }">
          <div class="flex h-full items-center justify-center">
            <LandingRodakIonIcon name="image-outline" :size="52" color="rgba(255,255,255,0.8)" />
          </div>
          <div class="absolute inset-x-0 bottom-0 flex h-[32px] items-center justify-between bg-black/55 px-[12px] text-[11px] text-white">
            <span>{{ DEMO_PHOTOS[selectedPhotoIdx]?.label }}</span>
            <span>{{ DEMO_PHOTOS[selectedPhotoIdx]?.date }}</span>
          </div>
        </div>
      </template>

      <!-- ==================== CAMERA APP (Exact camera_app.cc: 266x200 viewfinder left + 54px shutter rail right) ==================== -->
      <template v-else-if="scene === 'camera'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">相机</div>
          <div class="w-[40px]" />
        </div>

        <div class="flex h-[202px] w-full">
          <!-- Left 266x202 viewfinder -->
          <div
            class="relative h-full w-[266px] overflow-hidden"
            style="background: radial-gradient(circle at 46% 42%, #1e3a5f 0%, #0b1524 68%, #04070d 100%)"
          >
            <div class="absolute inset-x-0 top-1/3 h-[1px] bg-white/12" />
            <div class="absolute inset-x-0 top-2/3 h-[1px] bg-white/12" />
            <div class="absolute inset-y-0 left-1/3 w-[1px] bg-white/12" />
            <div class="absolute inset-y-0 left-2/3 w-[1px] bg-white/12" />
            <div class="absolute top-1/2 left-1/2 -mt-[21px] -ml-[21px] size-[42px] rounded-[8px] border-[1.5px] border-[#79CBFF]" />
            <div class="absolute bottom-[6px] left-[8px] rounded-[4px] bg-black/50 px-[6px] py-[2px] font-mono text-[10px] text-[#BED4E1]">
              GC0308 · 320×240 · #{{ cameraShots }}
            </div>
            <div v-if="cameraFlash" class="pointer-events-none absolute inset-0 bg-white/75" />
          </div>

          <!-- Right 54px shutter rail -->
          <div
            class="flex h-full w-[54px] flex-col items-center justify-between py-[16px]"
            :style="{ backgroundColor: t.bgPrimary }"
          >
            <span class="font-mono text-[9.5px]" :style="{ color: t.success }">DMA</span>
            <button
              type="button"
              class="flex size-[44px] cursor-pointer items-center justify-center rounded-full active:scale-90"
              :style="{ border: `3px solid ${t.textPrimary}` }"
              @click="captureCamera"
            >
              <span class="block size-[30px] rounded-full" :style="{ backgroundColor: t.textPrimary }" />
            </button>
            <button
              type="button"
              class="cursor-pointer font-mono text-[9.5px]"
              :style="{ color: t.textTertiary }"
              @click="openScene('photos')"
            >
              相册
            </button>
          </div>
        </div>
      </template>

      <!-- ==================== MESSAGES APP (Exact messages_app.cc filter bar + chips + detail footer) ==================== -->
      <template v-else-if="scene === 'messages'">
        <div
          class="relative flex h-[38px] items-center justify-between px-[10px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <button
            type="button"
            class="z-10 flex h-[26px] w-[40px] cursor-pointer items-center justify-center rounded-[6px]"
            :style="{ backgroundColor: t.bgTertiary }"
            @click="selectedMessageId ? (selectedMessageId = null) : goHome(false)"
          >
            <LandingRodakIonIcon name="arrow-back-outline" :size="17" :color="t.textPrimary" />
          </button>
          <div class="pointer-events-none absolute inset-x-0 text-center text-[15px] font-medium">
            {{ activeMessage ? '消息详情' : '消息中心' }}
          </div>
          <div class="w-[40px]" />
        </div>

        <template v-if="!activeMessage">
          <div
            class="flex h-[28px] items-center gap-[6px] px-[10px]"
            :style="{ borderBottom: `1px solid ${t.divider}` }"
          >
            <button
              v-for="tab in [
                { id: 'all' as const, label: '全部' },
                { id: 'todo' as const, label: '待办' },
                { id: 'notice' as const, label: '通知' }
              ]"
              :key="tab.id"
              type="button"
              class="cursor-pointer rounded-full px-[10px] py-[2px] text-[11.5px] font-medium"
              :style="{
                backgroundColor: msgFilter === tab.id ? t.primary : t.bgSecondary,
                color: msgFilter === tab.id ? t.onPrimary : t.textSecondary
              }"
              @click="msgFilter = tab.id"
            >
              {{ tab.label }}
            </button>
          </div>

          <div class="space-y-[8px] p-[10px]">
            <button
              v-for="m in filteredMessages"
              :key="m.id"
              type="button"
              class="flex w-full cursor-pointer flex-col gap-[5px] rounded-[10px] px-[12px] py-[9px] text-left"
              :style="{ backgroundColor: t.bgSecondary }"
              @click="selectedMessageId = m.id"
            >
              <div class="flex items-center justify-between gap-[6px]">
                <span class="inline-flex items-center gap-[6px]">
                  <span
                    class="rounded-[4px] px-[5px] py-[1px] text-[10.5px] font-medium"
                    :style="{
                      backgroundColor:
                        m.state === 'done'
                          ? `${t.success}33`
                          : m.kind === 'todo'
                            ? `${t.error}33`
                            : `${t.textTertiary}33`,
                      color:
                        m.state === 'done'
                          ? t.success
                          : m.kind === 'todo'
                            ? t.error
                            : t.textTertiary
                    }"
                  >
                    {{ m.state === 'done' ? '已完成' : m.state === 'snoozed' ? '已推迟' : m.kind === 'todo' ? '待办' : '通知' }}
                  </span>
                  <span class="text-[13px] font-medium">{{ m.title }}</span>
                </span>
                <span class="text-[11px]" :style="{ color: t.textTertiary }">{{ m.time }}</span>
              </div>
              <div class="truncate text-[11.5px]" :style="{ color: t.textSecondary }">{{ m.body }}</div>
            </button>
          </div>
        </template>

        <!-- Detail view + bottom footer (CreateFooter) -->
        <div v-else class="flex h-[202px] flex-col justify-between">
          <div class="space-y-[8px] p-[12px]">
            <div class="text-[14.5px] font-semibold">{{ activeMessage.title }}</div>
            <div class="text-[12.5px] leading-[18px]" :style="{ color: t.textSecondary }">{{ activeMessage.body }}</div>
          </div>
          <div
            v-if="activeMessage.kind === 'todo'"
            class="flex h-[50px] items-center gap-[8px] px-[10px]"
            :style="{ backgroundColor: t.bgSecondary, borderTop: `1px solid ${t.divider}` }"
          >
            <button
              type="button"
              class="flex h-[34px] flex-1 cursor-pointer items-center justify-center rounded-[8px] text-[13px] font-medium"
              :style="{ backgroundColor: t.primary, color: t.onPrimary }"
              @click="completeMessage(activeMessage.id)"
            >
              完成
            </button>
            <button
              type="button"
              class="flex h-[34px] flex-1 cursor-pointer items-center justify-center rounded-[8px] text-[13px] font-medium"
              :style="{ backgroundColor: t.bgTertiary, color: t.textPrimary }"
              @click="snoozeMessage(activeMessage.id)"
            >
              推迟 30m
            </button>
          </div>
        </div>
      </template>

      <!-- ==================== SHELL OVERLAY 1: VOICE CAPTION (yields to modal & toast) ==================== -->
      <button
        v-if="voiceOverlayText && !lockScreenOpen && !controlCenterOpen && !toastText && !screenSleeping"
        type="button"
        class="absolute right-[10px] bottom-[8px] left-[10px] z-20 cursor-pointer rounded-[16px] px-[12px] py-[7px] text-center text-[13.5px] leading-[18px] font-medium backdrop-blur-md active:scale-[0.98]"
        :style="{
          background: t.isLight
            ? 'linear-gradient(180deg, rgba(255,255,255,0.92) 0%, rgba(242,245,248,0.88) 100%)'
            : 'linear-gradient(180deg, rgba(56,62,72,0.86) 0%, rgba(24,27,32,0.92) 100%)',
          border: `1px solid ${t.isLight ? 'rgba(255,255,255,0.95)' : 'rgba(255,255,255,0.18)'}`,
          boxShadow: t.isLight ? '0 4px 14px rgba(0,0,0,0.14)' : '0 4px 14px rgba(0,0,0,0.55)',
          color: t.textPrimary
        }"
        @click="voiceOverlayText = ''"
      >
        {{ voiceOverlayText }}
      </button>

      <!-- ==================== SHELL OVERLAY 2: EXACT PhoneShell::CreateControlCenter() ==================== -->
      <div
        v-if="controlCenterOpen && !screenSleeping"
        class="absolute inset-0 z-30"
        :style="{ backgroundColor: t.bgPrimary, color: t.textPrimary }"
      >
        <div class="absolute top-[10px] left-[14px] text-[17px] font-medium">Control Center</div>
        <button
          type="button"
          class="absolute top-[6px] right-[8px] flex h-[28px] w-[36px] cursor-pointer items-center justify-center rounded-[6px] active:scale-95"
          :style="{ backgroundColor: t.bgTertiary }"
          @click="controlCenterOpen = false"
        >
          <LandingRodakIonIcon name="close-outline" :size="16" :color="t.textPrimary" />
        </button>

        <!-- Wi-Fi panel: 292x42 at y=43 -->
        <div
          class="absolute top-[43px] left-[14px] flex h-[42px] w-[292px] items-center gap-[12px] rounded-[8px] px-[12px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <LandingRodakIonIcon name="wifi-outline" :size="18" :color="t.textPrimary" />
          <span class="text-[13.5px]">Wi-Fi connected</span>
        </div>

        <!-- Brightness panel: 292x68 at y=93 -->
        <div
          class="absolute top-[93px] left-[14px] flex h-[68px] w-[292px] flex-col justify-between rounded-[8px] px-[12px] py-[8px]"
          :style="{ backgroundColor: t.bgSecondary }"
        >
          <div class="inline-flex items-center gap-[8px] text-[13.5px]">
            <LandingRodakIonIcon name="sunny-outline" :size="16" :color="t.textPrimary" />
            <span>Brightness</span>
            <span class="ml-auto text-[12px]" :style="{ color: t.textTertiary }">{{ brightness }}%</span>
          </div>
          <input v-model.number="brightness" type="range" min="5" max="100" class="mb-[4px] h-[8px] w-full cursor-pointer accent-sky-400">
        </div>

        <!-- Bottom 2 action buttons: 136x48 each (Settings / Lock) -->
        <button
          type="button"
          class="absolute bottom-[18px] left-[14px] flex h-[48px] w-[136px] cursor-pointer items-center justify-center gap-[8px] rounded-[8px] text-[13.5px] font-medium active:scale-95"
          :style="{ backgroundColor: t.bgTertiary, color: t.textPrimary }"
          @click="openScene('settings')"
        >
          <LandingRodakIonIcon name="settings-outline" :size="16" :color="t.textPrimary" />
          <span>Settings</span>
        </button>
        <button
          type="button"
          class="absolute right-[14px] bottom-[18px] flex h-[48px] w-[136px] cursor-pointer items-center justify-center gap-[8px] rounded-[8px] text-[13.5px] font-medium active:scale-95"
          :style="{ backgroundColor: t.bgTertiary, color: t.textPrimary }"
          @click="lockDevice(false)"
        >
          <LandingRodakIonIcon name="lock-closed-outline" :size="16" :color="t.textPrimary" />
          <span>Lock</span>
        </button>
      </div>

      <!-- ==================== SHELL OVERLAY 3: EXACT PhoneShell::CreateLockScreen() ==================== -->
      <div
        v-if="lockScreenOpen && !screenSleeping"
        class="absolute inset-0 z-40"
        :style="{ backgroundColor: t.bgPrimary, color: t.textPrimary }"
      >
        <div class="absolute top-[12px] left-[14px]">
          <LandingRodakIonIcon name="lock-closed-outline" :size="17" :color="t.textSecondary" />
        </div>
        <div class="absolute top-[12px] right-[14px]">
          <LandingRodakIonIcon name="wifi-outline" :size="17" :color="t.textSecondary" />
        </div>

        <div class="absolute inset-x-0 top-[68px] text-center">
          <div class="text-[28px] leading-[32px] font-semibold tracking-[0.5px]">{{ clockNow }}</div>
          <div class="mt-[6px] text-[13.5px]" :style="{ color: t.textSecondary }">Thu, 2026-11-26</div>
        </div>

        <!-- Privacy notice pill (counts only, matching UpdateLockNoticeHint) -->
        <div
          v-if="lockNoticeText"
          class="absolute top-[138px] left-1/2 inline-flex h-[26px] -translate-x-1/2 items-center gap-[6px] rounded-[13px] px-[12px] text-[11.5px] whitespace-nowrap"
          :style="{ backgroundColor: t.bgSecondary, border: `1px solid ${t.border}` }"
        >
          <LandingRodakIonIcon name="notifications-outline" :size="13" :color="t.error" />
          <span>{{ lockNoticeText }}</span>
        </div>

        <!-- Bottom swipe-up unlock area (swipe up or click Unlock cue) -->
        <button
          type="button"
          class="absolute inset-x-0 bottom-[6px] flex cursor-pointer flex-col items-center gap-[2px] py-[4px]"
          title="向上滑动（或点击）解锁"
          @click="lockScreenOpen = false"
        >
          <LandingRodakIonIcon name="arrow-up-outline" :size="17" :color="t.textPrimary" />
          <span class="text-[11.5px]" :style="{ color: t.textSecondary }">Unlock</span>
        </button>
      </div>

      <!-- ==================== HARDWARE POWER SLEEP OVERLAY (KEY_POWER / STC8G MCU) ==================== -->
      <button
        v-if="screenSleeping"
        type="button"
        class="group absolute inset-0 z-50 flex cursor-pointer items-center justify-center bg-[#040609] text-center"
        @click="togglePowerSleep"
      >
        <span class="rounded-full border border-white/10 bg-white/5 px-[12px] py-[4px] text-[11px] text-slate-400 opacity-75 transition-opacity group-hover:opacity-100">
          息屏待机 · 按 POWER 键或轻触亮屏
        </span>
      </button>

      <!-- ==================== AUTHENTIC PhoneUi TOAST BAR ==================== -->
      <div
        v-if="toastText && !screenSleeping"
        class="pointer-events-none absolute bottom-[10px] left-1/2 z-50 -translate-x-1/2 rounded-full px-[12px] py-[4px] text-[11.5px] font-medium whitespace-nowrap shadow-md"
        :style="{
          backgroundColor: t.isLight ? 'rgba(33,33,33,0.9)' : 'rgba(45,45,45,0.95)',
          color: '#FFFFFF',
          border: `1px solid ${t.border}`
        }"
      >
        {{ toastText }}
      </div>
    </div>
  </div>
</template>

<style scoped>
.rd-os-canvas {
  font-family: 'Segoe UI', 'Microsoft YaHei UI', 'PingFang SC', system-ui, sans-serif;
  font-variant-numeric: tabular-nums;
  -webkit-font-smoothing: antialiased;
  text-rendering: geometricPrecision;
}
</style>
