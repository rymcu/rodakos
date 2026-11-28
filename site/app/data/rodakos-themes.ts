import type { FirmwareThemeId } from '~/types/landing'

export type IconSlotName =
  | 'system'
  | 'media'
  | 'tools'
  | 'games'
  | 'neutral'
  | 'warm'
  | 'cool'
  | 'alert'

export interface RodakThemePalette {
  id: FirmwareThemeId
  primary: string
  secondary: string
  accent: string
  bgPrimary: string
  bgSecondary: string
  bgTertiary: string
  textPrimary: string
  textSecondary: string
  textTertiary: string
  success: string
  warning: string
  error: string
  border: string
  divider: string
  isLight: boolean
  onPrimary: string
  slots: Record<IconSlotName, string>
}

// Exact theme tokens from main/phone_ui/rodakos_theme.cc
export const RODAKOS_THEMES: Record<FirmwareThemeId, RodakThemePalette> = {
  dark: {
    id: 'dark',
    primary: '#79CBFF',
    secondary: '#F2C76E',
    accent: '#F27C7C',
    bgPrimary: '#000000',
    bgSecondary: '#1A1A1A',
    bgTertiary: '#2D2D2D',
    textPrimary: '#EAF7FF',
    textSecondary: '#BED4E1',
    textTertiary: '#8FB6C8',
    success: '#6FCF97',
    warning: '#F2C76E',
    error: '#F27C7C',
    border: '#3D3D3D',
    divider: '#262626',
    isLight: false,
    onPrimary: '#000000',
    slots: {
      system: '#5A6E8C',
      media: '#D0644E',
      tools: '#3D7DD3',
      games: '#3A9A5F',
      neutral: '#7A7068',
      warm: '#B87A22',
      cool: '#23919F',
      alert: '#C23A6E'
    }
  },
  light: {
    id: 'light',
    primary: '#2196F3',
    secondary: '#FF9800',
    accent: '#E91E63',
    bgPrimary: '#FFFFFF',
    bgSecondary: '#F5F5F5',
    bgTertiary: '#EBEBEB',
    textPrimary: '#212121',
    textSecondary: '#666666',
    textTertiary: '#9A9A9A',
    success: '#4CAF50',
    warning: '#FF9800',
    error: '#F44336',
    border: '#E0E0E0',
    divider: '#D6D6D6',
    isLight: true,
    onPrimary: '#000000',
    slots: {
      system: '#52637E',
      media: '#C4553E',
      tools: '#2F6CC2',
      games: '#2E8550',
      neutral: '#6E645C',
      warm: '#A86C17',
      cool: '#1C7F8C',
      alert: '#B2305F'
    }
  },
  blue: {
    id: 'blue',
    primary: '#1976D2',
    secondary: '#64B5F6',
    accent: '#00BCD4',
    bgPrimary: '#0D1B2A',
    bgSecondary: '#1B263B',
    bgTertiary: '#415A77',
    textPrimary: '#E0FBFC',
    textSecondary: '#C0E8F0',
    textTertiary: '#778DA9',
    success: '#4CAF50',
    warning: '#FF9800',
    error: '#F44336',
    border: '#2C3E50',
    divider: '#1E2A38',
    isLight: false,
    onPrimary: '#FFFFFF',
    slots: {
      system: '#667A98',
      media: '#D46A52',
      tools: '#4A8BE6',
      games: '#3E9E63',
      neutral: '#857A71',
      warm: '#BC7E26',
      cool: '#2496A0',
      alert: '#C8406F'
    }
  },
  green: {
    id: 'green',
    primary: '#388E3C',
    secondary: '#81C784',
    accent: '#FFEB3B',
    bgPrimary: '#1B2A1F',
    bgSecondary: '#263D2B',
    bgTertiary: '#3E5C45',
    textPrimary: '#F1F8E9',
    textSecondary: '#DCEDC8',
    textTertiary: '#AED581',
    success: '#66BB6A',
    warning: '#FFA726',
    error: '#EF5350',
    border: '#2E5033',
    divider: '#1F3823',
    isLight: false,
    onPrimary: '#FFFFFF',
    slots: {
      system: '#62759A',
      media: '#D2674F',
      tools: '#4082D8',
      games: '#2F9C86',
      neutral: '#847970',
      warm: '#BA7C24',
      cool: '#2A8FB0',
      alert: '#C63E6E'
    }
  }
}

export function darkenHex(hex: string, amount = 31 / 255): string {
  const clean = hex.replace('#', '')
  const r = Math.round(Number.parseInt(clean.slice(0, 2), 16) * (1 - amount))
  const g = Math.round(Number.parseInt(clean.slice(2, 4), 16) * (1 - amount))
  const b = Math.round(Number.parseInt(clean.slice(4, 6), 16) * (1 - amount))
  return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`
}

export interface CalendarCellData {
  d: number
  l: string
  out?: boolean
  wk?: boolean
  term?: boolean
  today?: boolean
  fullLunar?: string
}

export interface CalendarMonthData {
  key: string
  label: string
  cells: readonly CalendarCellData[]
}

export const CALENDAR_MONTHS_2026: readonly CalendarMonthData[] = [
  {
    key: '2026-10',
    label: '2026 年 10 月',
    cells: [
      { d: 28, l: '十八', out: true },
      { d: 29, l: '十九', out: true },
      { d: 30, l: '二十', out: true },
      { d: 1, l: '国庆', term: true },
      { d: 2, l: '廿二' },
      { d: 3, l: '廿三', wk: true },
      { d: 4, l: '廿四', wk: true },
      { d: 5, l: '廿五' },
      { d: 6, l: '廿六' },
      { d: 7, l: '廿七' },
      { d: 8, l: '寒露', term: true },
      { d: 9, l: '廿九' },
      { d: 10, l: '九月', wk: true },
      { d: 11, l: '初二', wk: true },
      { d: 12, l: '初三' },
      { d: 13, l: '初四' },
      { d: 14, l: '初五' },
      { d: 15, l: '初六' },
      { d: 16, l: '初七' },
      { d: 17, l: '初八', wk: true },
      { d: 18, l: '重阳', wk: true, term: true },
      { d: 19, l: '初十' },
      { d: 20, l: '十一' },
      { d: 21, l: '十二' },
      { d: 22, l: '十三' },
      { d: 23, l: '霜降', term: true },
      { d: 24, l: '十五', wk: true },
      { d: 25, l: '十六', wk: true },
      { d: 26, l: '十七' },
      { d: 27, l: '十八' },
      { d: 28, l: '十九' },
      { d: 29, l: '二十' },
      { d: 30, l: '廿一' },
      { d: 31, l: '廿二', wk: true },
      { d: 1, l: '廿三', out: true, wk: true },
      { d: 2, l: '廿四', out: true },
      { d: 3, l: '廿五', out: true },
      { d: 4, l: '廿六', out: true },
      { d: 5, l: '廿七', out: true },
      { d: 6, l: '廿八', out: true },
      { d: 7, l: '立冬', out: true, wk: true, term: true },
      { d: 8, l: '三十', out: true, wk: true }
    ]
  },
  {
    key: '2026-11',
    label: '2026 年 11 月',
    cells: [
      { d: 26, l: '十七', out: true },
      { d: 27, l: '十八', out: true },
      { d: 28, l: '十九', out: true },
      { d: 29, l: '二十', out: true },
      { d: 30, l: '廿一', out: true },
      { d: 31, l: '廿二', out: true, wk: true },
      { d: 1, l: '廿三', wk: true },
      { d: 2, l: '廿四' },
      { d: 3, l: '廿五' },
      { d: 4, l: '廿六' },
      { d: 5, l: '廿七' },
      { d: 6, l: '廿八' },
      { d: 7, l: '立冬', wk: true, term: true },
      { d: 8, l: '三十', wk: true },
      { d: 9, l: '十月' },
      { d: 10, l: '初二' },
      { d: 11, l: '初三' },
      { d: 12, l: '初四' },
      { d: 13, l: '初五' },
      { d: 14, l: '初六', wk: true },
      { d: 15, l: '初七', wk: true },
      { d: 16, l: '初八' },
      { d: 17, l: '初九' },
      { d: 18, l: '初十' },
      { d: 19, l: '十一' },
      { d: 20, l: '十二' },
      { d: 21, l: '十三', wk: true },
      { d: 22, l: '小雪', wk: true, term: true },
      { d: 23, l: '十五' },
      { d: 24, l: '十六' },
      { d: 25, l: '十七' },
      { d: 26, l: '十八', today: true },
      { d: 27, l: '十九' },
      { d: 28, l: '二十', wk: true },
      { d: 29, l: '廿一', wk: true },
      { d: 30, l: '廿二' },
      { d: 1, l: '廿三', out: true },
      { d: 2, l: '廿四', out: true },
      { d: 3, l: '廿五', out: true },
      { d: 4, l: '廿六', out: true },
      { d: 5, l: '廿七', out: true, wk: true },
      { d: 6, l: '廿八', out: true, wk: true }
    ]
  },
  {
    key: '2026-12',
    label: '2026 年 12 月',
    cells: [
      { d: 30, l: '廿二', out: true },
      { d: 1, l: '廿三' },
      { d: 2, l: '廿四' },
      { d: 3, l: '廿五' },
      { d: 4, l: '廿六' },
      { d: 5, l: '廿七', wk: true },
      { d: 6, l: '廿八', wk: true },
      { d: 7, l: '大雪', term: true },
      { d: 8, l: '三十' },
      { d: 9, l: '冬月' },
      { d: 10, l: '初二' },
      { d: 11, l: '初三' },
      { d: 12, l: '初四', wk: true },
      { d: 13, l: '初五', wk: true },
      { d: 14, l: '初六' },
      { d: 15, l: '初七' },
      { d: 16, l: '初八' },
      { d: 17, l: '初九' },
      { d: 18, l: '初十' },
      { d: 19, l: '十一', wk: true },
      { d: 20, l: '十二', wk: true },
      { d: 21, l: '十三' },
      { d: 22, l: '冬至', term: true },
      { d: 23, l: '十五' },
      { d: 24, l: '十六' },
      { d: 25, l: '十七' },
      { d: 26, l: '十八', wk: true },
      { d: 27, l: '十九', wk: true },
      { d: 28, l: '二十' },
      { d: 29, l: '廿一' },
      { d: 30, l: '廿二' },
      { d: 31, l: '廿三' },
      { d: 1, l: '元旦', out: true, term: true },
      { d: 2, l: '廿五', out: true, wk: true },
      { d: 3, l: '廿六', out: true, wk: true },
      { d: 4, l: '廿七', out: true },
      { d: 5, l: '小寒', out: true, term: true },
      { d: 6, l: '廿九', out: true },
      { d: 7, l: '三十', out: true },
      { d: 8, l: '腊月', out: true },
      { d: 9, l: '初二', out: true, wk: true },
      { d: 10, l: '初三', out: true, wk: true }
    ]
  }
]
