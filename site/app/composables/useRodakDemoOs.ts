import type { FirmwareThemeId } from '~/types/landing'

export interface SmartLightItem {
  id: string
  name: string
  on: boolean
  brightness: number
  color: string
  colorName: string
}

// Exact 6 presets from main/apps/smart/smart_app.cc (kPresets)
export const SMART_COLOR_SWATCHES = [
  { color: '#FFDCA9', name: '暖白' },
  { color: '#55B8FF', name: '冰蓝' },
  { color: '#2CE0D0', name: '青色' },
  { color: '#42DC78', name: '翠绿' },
  { color: '#FF5252', name: '珊瑚红' },
  { color: '#C855FF', name: '紫罗兰' }
] as const

export function useRodakDemoOs() {
  const firmwareTheme = useState<FirmwareThemeId>('rodakos-demo-theme', () => 'dark')
  const brightness = useState<number>('rodakos-demo-brightness', () => 75)
  const wakeEnabled = useState<boolean>('rodakos-demo-wake', () => true)
  const voiceLedEnabled = useState<boolean>('rodakos-demo-voice-led', () => true)
  const lockOnBoot = useState<boolean>('rodakos-demo-lock-boot', () => false)
  const controlCenterGestureEnabled = useState<boolean>('rodakos-demo-cc-gesture', () => true)
  const publisherTrusted = useState<boolean>('rodakos-demo-publisher-trusted', () => false)
  const unreadMessages = useState<number>('rodakos-demo-unread', () => 2)
  const openTodos = useState<number>('rodakos-demo-todos', () => 1)
  const lights = useState<SmartLightItem[]>('rodakos-demo-lights', () => [
    { id: 'board', name: '板载彩灯', on: true, brightness: 60, color: '#FFDCA9', colorName: '暖白' },
    { id: 'desk', name: '桌面灯带', on: true, brightness: 80, color: '#55B8FF', colorName: '冰蓝' },
    { id: 'bed', name: '床头氛围灯', on: false, brightness: 40, color: '#C855FF', colorName: '紫罗兰' }
  ])

  const boardLedColor = computed(() => {
    const board = lights.value[0]
    if (!board || !board.on) return voiceLedEnabled.value ? '#38bdf8' : 'transparent'
    return board.color
  })

  const boardLedIntensity = computed(() => {
    const board = lights.value[0]
    if (!board || !board.on) return voiceLedEnabled.value ? 0.55 : 0
    return Math.max(0.25, board.brightness / 100)
  })

  return {
    firmwareTheme,
    brightness,
    wakeEnabled,
    voiceLedEnabled,
    lockOnBoot,
    controlCenterGestureEnabled,
    publisherTrusted,
    unreadMessages,
    openTodos,
    lights,
    boardLedColor,
    boardLedIntensity
  }
}
