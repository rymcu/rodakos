/**
 * Redraws all 18 RodakOS 320x240 UI screens at crisp 4x Hi-DPI (1280x960) using
 * exact theme tokens from main/phone_ui/rodakos_theme.cc, exact Ionicons SVG artwork
 * from @iconify-json/ion (matching components/rodakos_fonts/icons.json), and faithful
 * layouts derived directly from the real LVGL 9.3 host renders.
 */

import { execFileSync, spawn } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const ROOT = path.resolve(__dirname, '..', '..')
const ION_JSON = path.join(ROOT, 'site', 'node_modules', '@iconify-json', 'ion', 'icons.json')
const OUT_DIR = path.join(ROOT, 'site', 'public', 'screenshots')

const ionData = JSON.parse(fs.readFileSync(ION_JSON, 'utf8'))

const CHROME_CANDIDATES = [
  'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
  'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe'
]
const CHROME = CHROME_CANDIDATES.find((p) => fs.existsSync(p))
if (!CHROME) {
  throw new Error('Could not find Chrome or Edge executable for headless Hi-DPI rendering.')
}

// Exact theme tokens from main/phone_ui/rodakos_theme.cc
const THEMES = {
  dark: {
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

function darkenHex(hex, amount = 31 / 255) {
  const clean = hex.replace('#', '')
  const r = Math.round(parseInt(clean.slice(0, 2), 16) * (1 - amount))
  const g = Math.round(parseInt(clean.slice(2, 4), 16) * (1 - amount))
  const b = Math.round(parseInt(clean.slice(4, 6), 16) * (1 - amount))
  return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`
}

function ionSvg(name, size, color = 'currentColor') {
  const icon = ionData.icons[name]
  if (!icon) throw new Error(`Missing ion icon: ${name}`)
  const w = icon.width ?? ionData.width ?? 512
  const h = icon.height ?? ionData.height ?? 512
  return `<svg width="${size}" height="${size}" viewBox="0 0 ${w} ${h}" style="color:${color};display:block;flex-shrink:0;" xmlns="http://www.w3.org/2000/svg">${icon.body}</svg>`
}

function pageWrap(bg, bodyHtml, extraCss = '') {
  return `<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body {
    width: 1280px;
    height: 960px;
    overflow: hidden;
    background: ${bg};
    -webkit-font-smoothing: antialiased;
    text-rendering: geometricPrecision;
  }
  .screen {
    width: 320px;
    height: 240px;
    zoom: 4;
    position: relative;
    overflow: hidden;
    background: ${bg};
    font-family: 'Segoe UI', 'Microsoft YaHei UI', 'PingFang SC', system-ui, sans-serif;
    font-variant-numeric: tabular-nums;
    user-select: none;
  }
  ${extraCss}
</style>
</head>
<body>
  <div class="screen">${bodyHtml}</div>
</body>
</html>`
}

// =========================================================================
// 1. HOME SCREEN (Page 1 in 4 themes + Page 2 + Voice Caption Overlay)
// =========================================================================
const HOME_PAGE1_APPS = [
  { title: 'Settings', icon: 'settings-outline', slot: 'system' },
  { title: 'Photos', icon: 'image-outline', slot: 'warm' },
  { title: 'Camera', icon: 'camera-outline', slot: 'media' },
  { title: 'Clock', icon: 'time-outline', slot: 'warm' },
  { title: 'Calendar', icon: 'calendar-number-outline', slot: 'tools' },
  { title: 'Files', icon: 'folder-outline', slot: 'neutral' },
  { title: 'Gyro', icon: 'compass-outline', slot: 'tools' },
  { title: 'System', icon: 'information-circle-outline', slot: 'system' },
  { title: 'Music', icon: 'musical-notes-outline', slot: 'media' },
  { title: 'Recorder', icon: 'mic-outline', slot: 'media' },
  { title: 'Assistant', icon: 'chatbubble-ellipses-outline', slot: 'cool' },
  { title: 'Smart', icon: 'bulb-outline', slot: 'cool' }
]

const HOME_PAGE2_APPS = [
  { title: 'Wake', icon: 'power-outline', slot: 'alert' },
  { title: 'Themes', icon: 'color-palette-outline', slot: 'system' },
  { title: 'Messages', icon: 'notifications-outline', slot: 'alert' }
]

function renderHome(themeKey, { page = 1, voiceCaption = '' } = {}) {
  const t = THEMES[themeKey]
  const apps = page === 1 ? HOME_PAGE1_APPS : HOME_PAGE2_APPS

  const tilesHtml = apps
    .map((app) => {
      const top = t.slots[app.slot]
      const bot = darkenHex(top, 31 / 255)
      return `
        <div class="tile">
          <div class="icon-box" style="background:linear-gradient(180deg, ${top} 0%, ${bot} 100%);">
            ${ionSvg(app.icon, 28, '#FFFFFF')}
          </div>
          <div class="tile-label" style="color:${t.textSecondary};">${app.title}</div>
        </div>`
    })
    .join('')

  const captionHtml = voiceCaption
    ? `<div class="voice-bar" style="
        background: ${
          t.isLight
            ? 'linear-gradient(180deg, rgba(255,255,255,0.88) 0%, rgba(242,245,248,0.82) 100%)'
            : 'linear-gradient(180deg, rgba(56,62,72,0.82) 0%, rgba(24,27,32,0.86) 100%)'
        };
        border: 1px solid ${t.isLight ? 'rgba(255,255,255,0.9)' : 'rgba(255,255,255,0.18)'};
        box-shadow: 0 4px 14px ${t.isLight ? 'rgba(0,0,0,0.14)' : 'rgba(0,0,0,0.55)'};
        color: ${t.textPrimary};
      ">${voiceCaption}</div>`
    : ''

  const dot1Active = page === 1
  const dotsHtml = `
    <div class="dots">
      <span class="dot" style="width:${dot1Active ? 18 : 6}px;background:${dot1Active ? t.secondary : t.border};"></span>
      <span class="dot" style="width:${!dot1Active ? 18 : 6}px;background:${!dot1Active ? t.secondary : t.border};"></span>
    </div>`

  return pageWrap(
    t.bgPrimary,
    `
    <div class="status-bar">
      <div class="clock-wrap">
        <span class="clock" style="color:${t.textPrimary};">10:08</span>
      </div>
      <div class="status-right">
        ${ionSvg('wifi-outline', 17, t.textPrimary)}
        ${ionSvg('battery-full-outline', 18, t.success)}
        <span class="batt-pct" style="color:${t.textTertiary};">92%</span>
      </div>
    </div>
    <div class="grid">${tilesHtml}</div>
    ${dotsHtml}
    ${captionHtml}
    `,
    `
    .status-bar {
      height: 24px;
      padding: 4px 14px 0 14px;
      display: flex;
      align-items: center;
      justify-content: space-between;
    }
    .clock-wrap { display: flex; align-items: center; gap: 6px; }
    .clock { font-size: 13.5px; font-weight: 500; letter-spacing: 0.2px; }
    .status-right { display: flex; align-items: center; gap: 6px; }
    .batt-pct { font-size: 12px; font-weight: 500; }
    .grid {
      display: grid;
      grid-template-columns: repeat(4, 64px);
      column-gap: 12px;
      row-gap: 6px;
      padding: 4px 14px 0 14px;
    }
    .tile {
      width: 64px;
      height: 60px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: flex-start;
    }
    .icon-box {
      width: 46px;
      height: 46px;
      border-radius: 14px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .tile-label {
      margin-top: 2px;
      font-size: 11px;
      line-height: 12px;
      font-weight: 400;
      text-align: center;
      white-space: nowrap;
    }
    .dots {
      position: absolute;
      left: 0;
      right: 0;
      bottom: 7px;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
    }
    .dot {
      height: 6px;
      border-radius: 999px;
    }
    .voice-bar {
      position: absolute;
      left: 12px;
      right: 12px;
      bottom: 8px;
      padding: 8px 12px;
      border-radius: 16px;
      backdrop-filter: blur(8px);
      text-align: center;
      font-size: 13.5px;
      font-weight: 500;
      line-height: 18px;
      letter-spacing: 0.2px;
    }
    `
  )
}

// =========================================================================
// 2. HEADER HELPER FOR STANDARD APPS
// =========================================================================
function appHeader(t, title, rightHtml = '', leftExtraHtml = '') {
  return `
    <div class="app-header" style="background:${t.bgSecondary};">
      <div class="header-left">
        <div class="hdr-btn" style="background:${t.bgTertiary};color:${t.textPrimary};">
          ${ionSvg('arrow-back-outline', 17, t.textPrimary)}
        </div>
        ${leftExtraHtml}
      </div>
      <div class="header-title" style="color:${t.textPrimary};">${title}</div>
      <div class="header-right">${rightHtml}</div>
    </div>`
}

const COMMON_HEADER_CSS = `
  .app-header {
    height: 38px;
    padding: 0 10px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    position: relative;
  }
  .header-left, .header-right {
    display: flex;
    align-items: center;
    gap: 6px;
    z-index: 2;
  }
  .header-title {
    position: absolute;
    left: 0;
    right: 0;
    text-align: center;
    font-size: 15px;
    font-weight: 500;
    pointer-events: none;
  }
  .hdr-btn {
    height: 26px;
    min-width: 38px;
    padding: 0 9px;
    border-radius: 7px;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 12.5px;
    font-weight: 500;
  }
`

// =========================================================================
// 3. CALENDAR APP (2026年11月 with full lunar dates & solar terms)
// =========================================================================
const CAL_ROWS = [
  [
    { d: 26, l: '十七', out: true },
    { d: 27, l: '十八', out: true },
    { d: 28, l: '十九', out: true },
    { d: 29, l: '二十', out: true },
    { d: 30, l: '廿一', out: true },
    { d: 31, l: '廿二', out: true, wk: true },
    { d: 1, l: '廿三', wk: true }
  ],
  [
    { d: 2, l: '廿四' },
    { d: 3, l: '廿五' },
    { d: 4, l: '廿六' },
    { d: 5, l: '廿七' },
    { d: 6, l: '廿八' },
    { d: 7, l: '立冬', wk: true, term: true },
    { d: 8, l: '三十', wk: true }
  ],
  [
    { d: 9, l: '十月' },
    { d: 10, l: '初二' },
    { d: 11, l: '初三' },
    { d: 12, l: '初四' },
    { d: 13, l: '初五' },
    { d: 14, l: '初六', wk: true },
    { d: 15, l: '初七', wk: true }
  ],
  [
    { d: 16, l: '初八' },
    { d: 17, l: '初九' },
    { d: 18, l: '初十' },
    { d: 19, l: '十一' },
    { d: 20, l: '十二' },
    { d: 21, l: '十三', wk: true },
    { d: 22, l: '小雪', wk: true, term: true }
  ],
  [
    { d: 23, l: '十五' },
    { d: 24, l: '十六' },
    { d: 25, l: '十七' },
    { d: 26, l: '十八', today: true },
    { d: 27, l: '十九' },
    { d: 28, l: '二十', wk: true },
    { d: 29, l: '廿一', wk: true }
  ],
  [
    { d: 30, l: '廿二' },
    { d: 1, l: '廿三', out: true },
    { d: 2, l: '廿四', out: true },
    { d: 3, l: '廿五', out: true },
    { d: 4, l: '廿六', out: true },
    { d: 5, l: '廿七', out: true, wk: true },
    { d: 6, l: '廿八', out: true, wk: true }
  ]
]

function renderCalendar(themeKey) {
  const t = THEMES[themeKey]
  const wkDays = ['一', '二', '三', '四', '五', '六', '日']
  const wkHtml = wkDays
    .map(
      (w, idx) =>
        `<div class="wk" style="color:${idx >= 5 ? t.secondary : t.textTertiary};">${w}</div>`
    )
    .join('')

  const cellsHtml = CAL_ROWS.flat()
    .map((c) => {
      let dayColor = c.wk ? t.secondary : t.textPrimary
      let lunColor = c.term ? t.secondary : t.textTertiary
      let bg = 'transparent'
      if (c.out) {
        dayColor = c.wk
          ? t.isLight
            ? 'rgba(255,152,0,0.42)'
            : 'rgba(242,199,110,0.34)'
          : t.isLight
            ? 'rgba(117,117,117,0.48)'
            : 'rgba(190,212,225,0.32)'
        lunColor = t.isLight ? 'rgba(158,158,158,0.45)' : 'rgba(143,182,200,0.28)'
      }
      if (c.today) {
        bg = t.primary
        dayColor = t.onPrimary
        lunColor = t.onPrimary
      }
      return `
        <div class="cal-cell" style="background:${bg};">
          <div class="cal-d" style="color:${dayColor};">${c.d}</div>
          <div class="cal-l" style="color:${lunColor};">${c.l}</div>
        </div>`
    })
    .join('')

  const prevBtn = `<div class="hdr-btn" style="min-width:30px;padding:0;background:${t.bgTertiary};">${ionSvg('chevron-back-outline', 15, t.textPrimary)}</div>`
  const rightBtns = `
    <div class="hdr-btn" style="min-width:30px;padding:0;background:${t.bgTertiary};">${ionSvg('chevron-forward-outline', 15, t.textPrimary)}</div>
    <div class="hdr-btn" style="min-width:44px;background:${t.bgTertiary};color:${t.textPrimary};">今天</div>`

  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '2026 年 11 月', rightBtns, prevBtn)}
    <div class="wk-row">${wkHtml}</div>
    <div class="cal-grid">${cellsHtml}</div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .wk-row {
      display: grid;
      grid-template-columns: repeat(7, 1fr);
      padding: 3px 8px 2px 8px;
      text-align: center;
    }
    .wk { font-size: 12px; font-weight: 500; line-height: 15px; }
    .cal-grid {
      display: grid;
      grid-template-columns: repeat(7, 1fr);
      grid-template-rows: repeat(6, 29px);
      padding: 2px 8px 4px 8px;
      gap: 1px 2px;
    }
    .cal-cell {
      border-radius: 7px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 1px;
    }
    .cal-d { font-size: 12.5px; font-weight: 600; line-height: 13px; }
    .cal-l { font-size: 9.5px; font-weight: 400; line-height: 10px; }
    `
  )
}

// =========================================================================
// 4. GYRO APP (154x154 Bubble Level)
// =========================================================================
function renderGyro(themeKey) {
  const t = THEMES[themeKey]
  const dialFill = t.isLight ? '#F8F9FA' : '#14171A'
  const ringStroke = t.isLight ? '#DCDFE4' : '#353A40'
  const dashStroke = t.isLight ? '#E5E8EC' : '#263038'
  const crossStroke = t.isLight ? '#E2E5E9' : '#2B3036'
  const horizonStroke = t.isLight ? '#8EC5FA' : '#5E8CA8'
  const bubbleCenter = t.isLight ? '#FFFFFF' : '#000000'
  const subReadout = t.isLight ? 'ω 0.2 °/s · 1.00 g' : 'ω 26.4 °/s · 1.02 g'

  const dialSvg = `
    <svg width="150" height="150" viewBox="0 0 150 150" xmlns="http://www.w3.org/2000/svg">
      <circle cx="75" cy="75" r="72" fill="${dialFill}" stroke="${ringStroke}" stroke-width="2" />
      <circle cx="75" cy="75" r="48" fill="none" stroke="${dashStroke}" stroke-width="1.2" stroke-dasharray="4 4" />
      <line x1="14" y1="75" x2="136" y2="75" stroke="${crossStroke}" stroke-width="1" />
      <line x1="75" y1="14" x2="75" y2="136" stroke="${crossStroke}" stroke-width="1" />
      <circle cx="75" cy="75" r="18" fill="none" stroke="${ringStroke}" stroke-width="1.6" />
      <line x1="33" y1="60" x2="117" y2="90" stroke="${horizonStroke}" stroke-width="2" stroke-linecap="round" />
      <circle cx="75" cy="75" r="2.6" fill="${t.textTertiary}" />
      <polygon points="118,32 108,26 119,21" fill="${t.secondary}" transform="rotate(14 114 27)" />
      <circle cx="94" cy="66" r="14" fill="${t.primary}" />
      <circle cx="94" cy="66" r="3.6" fill="${bubbleCenter}" />
    </svg>`

  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('hardware-chip-outline', 18, t.textPrimary)}</div>`

  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '陀螺仪', rightBtn)}
    <div class="gyro-body">
      <div class="gyro-left">
        ${dialSvg}
        <div class="omega" style="color:${t.textTertiary};">${subReadout}</div>
      </div>
      <div class="gyro-right">
        <div class="status-line" style="color:${t.textPrimary};">
          <span class="hollow-dot" style="border-color:${t.textTertiary};"></span>
          <span>倾斜中</span>
        </div>
        <div class="metrics-card" style="background:${t.bgSecondary};">
          <div class="m-row" style="border-bottom:1px solid ${t.divider};">
            <span class="m-lbl" style="color:${t.textSecondary};">横滚</span>
            <span class="m-val" style="color:${t.textPrimary};">+18.4°</span>
          </div>
          <div class="m-row" style="border-bottom:1px solid ${t.divider};">
            <span class="m-lbl" style="color:${t.textSecondary};">俯仰</span>
            <span class="m-val" style="color:${t.textPrimary};">-9.2°</span>
          </div>
          <div class="m-row">
            <span class="m-lbl" style="color:${t.textSecondary};">偏航</span>
            <span class="m-val" style="color:${t.textSecondary};">+42°</span>
          </div>
        </div>
        <div class="gyro-actions">
          <div class="g-btn" style="background:${t.bgSecondary};color:${t.textPrimary};">归零</div>
          <div class="g-btn" style="background:${t.bgSecondary};color:${t.textPrimary};">定格</div>
        </div>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .gyro-body {
      height: 202px;
      padding: 10px 10px 8px 12px;
      display: flex;
      gap: 12px;
    }
    .gyro-left {
      width: 152px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: space-between;
    }
    .omega { font-size: 11.5px; font-weight: 500; }
    .gyro-right {
      flex: 1;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
    }
    .status-line {
      display: flex;
      align-items: center;
      gap: 6px;
      font-size: 13.5px;
      font-weight: 500;
      padding-left: 2px;
    }
    .hollow-dot {
      width: 7px;
      height: 7px;
      border-radius: 50%;
      border: 1.5px solid;
    }
    .metrics-card {
      border-radius: 10px;
      overflow: hidden;
    }
    .m-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 8px 10px;
      font-size: 13px;
    }
    .m-val { font-weight: 600; font-size: 13.5px; }
    .gyro-actions {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px;
    }
    .g-btn {
      height: 36px;
      border-radius: 9px;
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 13.5px;
      font-weight: 500;
    }
    `
  )
}

// =========================================================================
// 5. SMART APP (Dark = 3-light overview; Light = single-light color & brightness)
// =========================================================================
function toggleSwitch(on, onColor, offBg) {
  return `<div class="sw" style="background:${on ? onColor : offBg};justify-content:${on ? 'flex-end' : 'flex-start'};"><span class="sw-knob"></span></div>`
}

function renderSmartDark() {
  const t = THEMES.dark
  const cards = [
    { name: '板载彩灯', sub: '已开启 · 60%', on: true, bulbBg: '#FFDCA9', bulbColor: '#1A1A1A' },
    { name: '桌面灯带', sub: '已开启 · 80%', on: true, bulbBg: '#55B8FF', bulbColor: '#0A1622' },
    { name: '床头氛围灯', sub: '已关闭', on: false, bulbBg: '#2B3036', bulbColor: '#7A96AA' }
  ]
  const cardsHtml = cards
    .map(
      (c) => `
      <div class="s-card" style="background:${t.bgSecondary};">
        <div class="s-top">
          <div class="bulb-circle" style="background:${c.bulbBg};">${ionSvg('bulb-outline', 18, c.bulbColor)}</div>
          ${toggleSwitch(c.on, t.primary, t.bgTertiary)}
        </div>
        <div class="s-name" style="color:${t.textPrimary};">${c.name}</div>
        <div class="s-sub" style="color:${c.on ? t.textSecondary : t.textTertiary};">
          <span class="s-dot" style="${c.on ? `background:${t.success};` : `border:1.5px solid ${t.textTertiary};`}"></span>
          <span>${c.sub}</span>
        </div>
      </div>`
    )
    .join('')

  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '智能家居 · 3')}
    <div class="s-grid">${cardsHtml}</div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .s-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      padding: 10px;
    }
    .s-card {
      height: 86px;
      border-radius: 11px;
      padding: 9px 10px;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
    }
    .s-top { display: flex; align-items: center; justify-content: space-between; }
    .bulb-circle {
      width: 30px;
      height: 30px;
      border-radius: 50%;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .sw {
      width: 38px;
      height: 22px;
      border-radius: 999px;
      padding: 2.5px;
      display: flex;
      align-items: center;
    }
    .sw-knob { width: 17px; height: 17px; border-radius: 50%; background: #FFFFFF; }
    .s-name { font-size: 13.5px; font-weight: 500; }
    .s-sub { display: flex; align-items: center; gap: 5px; font-size: 11.5px; }
    .s-dot { width: 6.5px; height: 6.5px; border-radius: 50%; }
    `
  )
}

function renderSmartLight() {
  const t = THEMES.light
  const swatches = [
    { color: '#FFDCA9', active: true },
    { color: '#45B0FF', active: false },
    { color: '#1DE0C5', active: false },
    { color: '#38DF6C', active: false },
    { color: '#FF5349', active: false },
    { color: '#BD52FF', active: false }
  ]
  const swatchHtml = swatches
    .map(
      (s) => `
      <div class="swatch" style="background:${s.color};${s.active ? 'box-shadow:inset 0 0 0 1.8px #212121;' : ''}">
        ${s.active ? ionSvg('checkmark-outline', 16, '#212121') : ''}
      </div>`
    )
    .join('')

  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '智能家居')}
    <div class="sl-body">
      <div class="sl-card sl-head" style="background:${t.bgSecondary};">
        <div class="bulb-big" style="background:#FFDCA9;">${ionSvg('bulb-outline', 22, '#212121')}</div>
        <div class="sl-info">
          <div class="sl-name" style="color:${t.textPrimary};">板载彩灯</div>
          <div class="sl-sub" style="color:${t.textSecondary};">
            <span class="sl-dot" style="background:${t.success};"></span>
            <span>已开启 · 60%</span>
          </div>
        </div>
        ${toggleSwitch(true, t.primary, t.bgTertiary)}
      </div>

      <div class="sl-card" style="background:${t.bgSecondary};padding:9px 12px 11px;">
        <div class="sl-row">
          <span class="sl-row-lbl" style="color:${t.textSecondary};">${ionSvg('sunny-outline', 16, t.primary)} 亮度</span>
          <span class="sl-pct" style="color:${t.textPrimary};">60%</span>
        </div>
        <div class="slider-track" style="background:${t.bgTertiary};">
          <div class="slider-fill" style="width:60%;background:#FFDCA9;"></div>
          <div class="slider-knob" style="left:60%;background:#FFDCA9;border:2px solid #FFFFFF;"></div>
        </div>
      </div>

      <div class="sl-card" style="background:${t.bgSecondary};padding:8px 12px 10px;">
        <div class="sl-row" style="margin-bottom:7px;">
          <span class="sl-row-lbl" style="color:${t.textSecondary};">颜色</span>
          <span class="sl-color-name" style="color:${t.textTertiary};">暖白</span>
        </div>
        <div class="swatches">${swatchHtml}</div>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .sl-body { padding: 8px 10px; display: flex; flex-direction: column; gap: 8px; }
    .sl-card { border-radius: 11px; }
    .sl-head {
      padding: 10px 12px;
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .bulb-big {
      width: 42px;
      height: 42px;
      border-radius: 50%;
      display: flex;
      align-items: center;
      justify-content: center;
      flex-shrink: 0;
    }
    .sl-info { flex: 1; display: flex; flex-direction: column; gap: 3px; }
    .sl-name { font-size: 14px; font-weight: 600; }
    .sl-sub { display: flex; align-items: center; gap: 5px; font-size: 12px; }
    .sl-dot { width: 7px; height: 7px; border-radius: 50%; }
    .sw {
      width: 44px;
      height: 25px;
      border-radius: 999px;
      padding: 3px;
      display: flex;
      align-items: center;
    }
    .sw-knob { width: 19px; height: 19px; border-radius: 50%; background: #FFFFFF; }
    .sl-row { display: flex; align-items: center; justify-content: space-between; font-size: 13px; }
    .sl-row-lbl { display: inline-flex; align-items: center; gap: 6px; font-weight: 500; }
    .sl-pct { font-size: 14px; font-weight: 600; }
    .sl-color-name { font-size: 12.5px; }
    .slider-track {
      margin-top: 10px;
      height: 7px;
      border-radius: 999px;
      position: relative;
    }
    .slider-fill { height: 100%; border-radius: 999px; }
    .slider-knob {
      position: absolute;
      top: 50%;
      width: 14px;
      height: 14px;
      border-radius: 50%;
      transform: translate(-50%, -50%);
      box-shadow: 0 1px 4px rgba(0,0,0,0.18);
    }
    .swatches { display: grid; grid-template-columns: repeat(6, 1fr); gap: 7px; }
    .swatch {
      height: 28px;
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    `
  )
}

// =========================================================================
// 6. THEMES APP (Dark & Light)
// =========================================================================
function renderThemes(themeKey) {
  const t = THEMES[themeKey]
  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('home-outline', 17, t.textPrimary)}</div>`

  const presets = [
    { key: 'L', bg: '#FFFFFF', fg: '#212121', active: themeKey === 'light' },
    { key: 'D', bg: '#0B0B0B', fg: '#F4F4F5', active: themeKey === 'dark' },
    { key: 'B', bg: '#1976D2', fg: '#FFFFFF', active: false },
    { key: 'G', bg: '#388E3C', fg: '#FFFFFF', active: false }
  ]

  const presetBtns = presets
    .map(
      (p) => `
      <div class="p-btn" style="background:${p.bg};color:${p.fg};border:${
        p.active
          ? `2px solid ${t.isLight ? '#212121' : '#FFFFFF'}`
          : `1px solid ${t.isLight ? '#D4D4D8' : '#3F3F46'}`
      };">${p.key}</div>`
    )
    .join('')

  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '主题', rightBtn)}
    <div class="th-body">
      <div class="th-card th-preview-card" style="background:${t.bgSecondary};">
        <div class="mini-screen" style="background:${t.bgPrimary};border:1px solid ${t.border};">
          <div class="mini-bar" style="background:${t.bgSecondary};"></div>
          <div class="mini-icons">
            <span class="mini-ico" style="background:${t.slots.system};"></span>
            <span class="mini-ico" style="background:${t.slots.media};"></span>
            <span class="mini-ico" style="background:${t.slots.tools};"></span>
          </div>
          <div class="mini-bottom">
            <span class="mini-pill" style="background:${t.primary};"></span>
            <span class="mini-aa" style="color:${t.textPrimary};">Aa</span>
          </div>
        </div>
        <div class="th-meta">
          <div class="th-title" style="color:${t.textPrimary};">配色：本地</div>
          <div class="th-sub" style="color:${t.textSecondary};">内置外观</div>
          <div class="th-sub" style="color:${t.textSecondary};">已生效</div>
        </div>
      </div>

      <div class="th-card th-row" style="background:${t.bgSecondary};">
        <div class="th-row-lbl" style="color:${t.textPrimary};">
          ${ionSvg('color-palette-outline', 18, t.primary)}
          <span>配色</span>
        </div>
        <div class="p-list">${presetBtns}</div>
      </div>

      <div class="th-card th-row" style="background:${t.bgSecondary};">
        <div class="th-row-lbl" style="color:${t.textPrimary};">
          ${ionSvg('lock-closed-outline', 17, t.primary)}
          <span>发布端：未信任</span>
        </div>
        ${ionSvg('chevron-forward-outline', 16, t.textTertiary)}
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .th-body { padding: 8px 10px; display: flex; flex-direction: column; gap: 8px; }
    .th-card { border-radius: 11px; }
    .th-preview-card {
      padding: 10px;
      display: flex;
      align-items: center;
      gap: 14px;
    }
    .mini-screen {
      width: 112px;
      height: 78px;
      border-radius: 8px;
      overflow: hidden;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      flex-shrink: 0;
    }
    .mini-bar { height: 14px; width: 100%; }
    .mini-icons {
      display: flex;
      justify-content: center;
      gap: 10px;
      padding-top: 2px;
    }
    .mini-ico { width: 20px; height: 20px; border-radius: 6px; }
    .mini-bottom {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 0 12px 10px;
    }
    .mini-pill { width: 46px; height: 13px; border-radius: 999px; }
    .mini-aa { font-size: 12px; font-weight: 600; }
    .th-meta { display: flex; flex-direction: column; gap: 4px; }
    .th-title { font-size: 14.5px; font-weight: 500; }
    .th-sub { font-size: 13px; }
    .th-row {
      padding: 9px 12px;
      display: flex;
      align-items: center;
      justify-content: space-between;
    }
    .th-row-lbl { display: inline-flex; align-items: center; gap: 8px; font-size: 13.5px; font-weight: 500; }
    .p-list { display: flex; align-items: center; gap: 7px; }
    .p-btn {
      width: 40px;
      height: 24px;
      border-radius: 7px;
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 12px;
      font-weight: 600;
    }
    `
  )
}

// =========================================================================
// 7. ASSISTANT APP (Dark)
// =========================================================================
function renderAssistant() {
  const t = THEMES.dark
  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('information-circle-outline', 18, t.textPrimary)}</div>`

  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '语音助手', rightBtn)}
    <div class="as-body">
      <div class="as-hero" style="background:${t.bgSecondary};">
        <div class="as-avatar" style="background:#314354;">
          ${ionSvg('chatbubble-ellipses-outline', 22, t.primary)}
        </div>
        <div class="as-status">
          <div class="as-state" style="color:${t.textPrimary};">
            <span class="as-dot" style="background:${t.success};"></span>
            <span>待命中</span>
          </div>
          <div class="as-hint" style="color:${t.textSecondary};">说「你好达克」开始对话</div>
        </div>
      </div>

      <div class="as-group" style="background:${t.bgSecondary};">
        <div class="as-row" style="border-bottom:1px solid ${t.divider};">
          <span class="as-lbl" style="color:${t.textPrimary};">${ionSvg('mic-outline', 17, t.primary)} 语音唤醒</span>
          ${toggleSwitch(true, t.primary, t.bgTertiary)}
        </div>
        <div class="as-row" style="border-bottom:1px solid ${t.divider};">
          <span class="as-lbl" style="color:${t.textPrimary};">${ionSvg('bulb-outline', 17, t.primary)} 语音状态灯</span>
          <div class="as-val-grp">
            <span style="color:${t.textTertiary};font-size:12px;">30%</span>
            ${toggleSwitch(true, t.primary, t.bgTertiary)}
          </div>
        </div>
        <div class="as-row">
          <span class="as-lbl" style="color:${t.textPrimary};">${ionSvg('cloud-outline', 17, t.primary)} 连接服务</span>
          <div class="as-val-grp">
            <span class="as-dot" style="background:${t.success};"></span>
            <span style="color:${t.success};font-size:13px;font-weight:500;">已连接</span>
            ${ionSvg('chevron-forward-outline', 14, t.textTertiary)}
          </div>
        </div>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .as-body { padding: 9px 10px; display: flex; flex-direction: column; gap: 9px; }
    .as-hero {
      border-radius: 12px;
      padding: 12px 14px;
      display: flex;
      align-items: center;
      gap: 14px;
    }
    .as-avatar {
      width: 44px;
      height: 44px;
      border-radius: 12px;
      display: flex;
      align-items: center;
      justify-content: center;
      flex-shrink: 0;
    }
    .as-status { display: flex; flex-direction: column; gap: 4px; }
    .as-state { display: flex; align-items: center; gap: 6px; font-size: 14.5px; font-weight: 500; }
    .as-dot { width: 7px; height: 7px; border-radius: 50%; display: inline-block; }
    .as-hint { font-size: 13px; }
    .as-group { border-radius: 12px; overflow: hidden; }
    .as-row {
      height: 38px;
      padding: 0 12px;
      display: flex;
      align-items: center;
      justify-content: space-between;
    }
    .as-lbl { display: inline-flex; align-items: center; gap: 9px; font-size: 13.5px; font-weight: 500; }
    .as-val-grp { display: inline-flex; align-items: center; gap: 7px; }
    .sw {
      width: 40px;
      height: 22px;
      border-radius: 999px;
      padding: 2.5px;
      display: flex;
      align-items: center;
    }
    .sw-knob { width: 17px; height: 17px; border-radius: 50%; background: #FFFFFF; }
    `
  )
}

// =========================================================================
// 8. RECORDER APP (Dark, 20-bar 48 dB real-time level meter)
// =========================================================================
function renderRecorder() {
  const t = THEMES.dark
  const rightBtns = `
    <div style="display:flex;align-items:center;margin-right:4px;">${ionSvg('mic-off-outline', 18, t.textTertiary)}</div>
    <div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('list-outline', 18, t.textPrimary)}</div>`

  const barsHtml = Array.from({ length: 20 }, (_, i) => {
    const active = i >= 11
    return `<span class="meter-bar" style="
      height:${active ? 26 : 3}px;
      background:${active ? t.secondary : '#2F3338'};
    "></span>`
  }).join('')

  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '录音', rightBtns)}
    <div class="rec-body">
      <div class="rec-status" style="color:${t.accent};">
        <span class="rec-dot" style="background:${t.accent};"></span>
        <span>正在录音 · 32 KB</span>
      </div>
      <div class="rec-timer" style="color:${t.accent};">0:01</div>
      <div class="meter-box" style="background:${t.bgSecondary};">${barsHtml}</div>
      <div class="rec-controls">
        <div class="rec-side-btn" style="background:${t.bgSecondary};color:${t.textTertiary};">
          ${ionSvg('volume-high-outline', 16, t.textTertiary)} 试音
        </div>
        <div class="shutter" style="border:2.5px solid ${t.accent};">
          <div class="shutter-stop" style="background:${t.accent};"></div>
        </div>
        <div class="rec-side-btn" style="background:${t.bgSecondary};color:${t.textTertiary};">
          ${ionSvg('play-outline', 15, t.textTertiary)} 回放
        </div>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .rec-body {
      height: 202px;
      padding: 10px 12px 12px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: space-between;
    }
    .rec-status {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      font-size: 13px;
      font-weight: 500;
    }
    .rec-dot { width: 7px; height: 7px; border-radius: 50%; }
    .rec-timer { font-size: 24px; font-weight: 600; line-height: 26px; letter-spacing: 0.5px; }
    .meter-box {
      width: 100%;
      height: 46px;
      border-radius: 11px;
      padding: 0 14px;
      display: flex;
      align-items: center;
      justify-content: space-between;
    }
    .meter-bar {
      width: 7px;
      border-radius: 999px;
    }
    .rec-controls {
      width: 100%;
      padding: 0 12px;
      display: flex;
      align-items: center;
      justify-content: space-between;
    }
    .rec-side-btn {
      width: 78px;
      height: 36px;
      border-radius: 10px;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 5px;
      font-size: 13px;
    }
    .shutter {
      width: 52px;
      height: 52px;
      border-radius: 50%;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .shutter-stop {
      width: 20px;
      height: 20px;
      border-radius: 5px;
    }
    `
  )
}

// =========================================================================
// 9. WAKE-ON-LAN APP (Dark & Light)
// =========================================================================
function renderWol(themeKey) {
  const t = THEMES[themeKey]
  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('add-outline', 18, t.textPrimary)}</div>`
  const devices =
    themeKey === 'dark'
      ? [
          { name: '书房台式机', mac: 'AA:BB:CC:DD:EE:FF · 端口 9' },
          { name: '客厅 NAS', mac: 'D8:3A:DD:42:19:0C · 端口 9' }
        ]
      : [{ name: '书房台式机', mac: 'AA:BB:CC:DD:EE:FF · 端口 9' }]

  const cardsHtml = devices
    .map(
      (d) => `
      <div class="w-card" style="background:${t.bgSecondary};">
        <div class="w-info">
          <div class="w-name" style="color:${t.textPrimary};">${d.name}</div>
          <div class="w-mac" style="color:${t.textTertiary};">${d.mac}</div>
        </div>
        <div class="w-sep" style="background:${t.border};"></div>
        <div class="w-pwr" style="background:${t.primary};">
          ${ionSvg('power-outline', 20, t.onPrimary)}
        </div>
      </div>`
    )
    .join('')

  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '网络唤醒', rightBtn)}
    <div class="w-strip" style="border-bottom:1px solid ${t.divider};">
      <div class="w-ssid" style="color:${t.textSecondary};">
        <span class="w-dot" style="background:${t.success};"></span>
        <span>书房-5G</span>
      </div>
      <div class="w-cnt" style="color:${t.textTertiary};">${devices.length} / 8</div>
    </div>
    <div class="w-list">${cardsHtml}</div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .w-strip {
      height: 24px;
      padding: 0 12px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      font-size: 12px;
    }
    .w-ssid { display: inline-flex; align-items: center; gap: 6px; font-weight: 500; }
    .w-dot { width: 6.5px; height: 6.5px; border-radius: 50%; }
    .w-list { padding: 9px 10px; display: flex; flex-direction: column; gap: 9px; }
    .w-card {
      border-radius: 11px;
      padding: 10px 10px 10px 12px;
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .w-info { flex: 1; display: flex; flex-direction: column; gap: 4px; }
    .w-name { font-size: 14.5px; font-weight: 500; }
    .w-mac { font-size: 11px; letter-spacing: 0.2px; }
    .w-sep { width: 1px; height: 34px; opacity: 0.6; }
    .w-pwr {
      width: 44px;
      height: 40px;
      border-radius: 9px;
      display: flex;
      align-items: center;
      justify-content: center;
      flex-shrink: 0;
    }
    `
  )
}

// =========================================================================
// 10. ASSISTANT THINKING, THEMES TRUST & REMAINING 8 APPS (for 3D CAD LCD)
// =========================================================================
function renderAssistantThinking() {
  const t = THEMES.dark
  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('information-circle-outline', 18, t.textPrimary)}</div>`
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '语音助手', rightBtn)}
    <div class="as-body">
      <div class="as-hero" style="background:${t.bgSecondary};">
        <div class="as-avatar" style="background:${t.primary};">
          ${ionSvg('chatbubble-ellipses-outline', 22, '#0A1622')}
        </div>
        <div class="as-status">
          <div class="as-state" style="color:${t.primary};">
            <span class="as-dot" style="background:${t.primary};"></span>
            <span>正在思考</span>
          </div>
          <div class="as-hint" style="color:${t.textSecondary};">等待罗达克回复...</div>
        </div>
      </div>

      <div class="as-group" style="background:${t.bgSecondary};">
        <div class="as-row" style="border-bottom:1px solid ${t.divider};">
          <span class="as-lbl" style="color:${t.textPrimary};">${ionSvg('mic-outline', 17, t.primary)} 语音唤醒</span>
          <div class="as-val-grp">
            <span style="color:${t.textSecondary};font-size:12px;">对话中</span>
            ${toggleSwitch(true, t.primary, t.bgTertiary)}
          </div>
        </div>
        <div class="as-row" style="border-bottom:1px solid ${t.divider};">
          <span class="as-lbl" style="color:${t.textPrimary};">${ionSvg('bulb-outline', 17, t.primary)} 语音状态灯</span>
          <div class="as-val-grp">
            <span style="color:${t.textTertiary};font-size:12px;">30%</span>
            ${toggleSwitch(true, t.primary, t.bgTertiary)}
          </div>
        </div>
        <div class="as-row">
          <span class="as-lbl" style="color:${t.textPrimary};">${ionSvg('cloud-outline', 17, t.primary)} 连接服务</span>
          <div class="as-val-grp">
            <span class="as-dot" style="background:${t.success};"></span>
            <span style="color:${t.success};font-size:13px;font-weight:500;">已连接</span>
            ${ionSvg('chevron-forward-outline', 14, t.textTertiary)}
          </div>
        </div>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .as-body { padding: 9px 10px; display: flex; flex-direction: column; gap: 9px; }
    .as-hero { border-radius: 12px; padding: 12px 14px; display: flex; align-items: center; gap: 14px; }
    .as-avatar { width: 44px; height: 44px; border-radius: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0; }
    .as-status { display: flex; flex-direction: column; gap: 4px; }
    .as-state { display: flex; align-items: center; gap: 6px; font-size: 14.5px; font-weight: 500; }
    .as-dot { width: 7px; height: 7px; border-radius: 50%; display: inline-block; }
    .as-hint { font-size: 13px; }
    .as-group { border-radius: 12px; overflow: hidden; }
    .as-row { height: 38px; padding: 0 12px; display: flex; align-items: center; justify-content: space-between; }
    .as-lbl { display: inline-flex; align-items: center; gap: 9px; font-size: 13.5px; font-weight: 500; }
    .as-val-grp { display: inline-flex; align-items: center; gap: 7px; }
    .sw { width: 40px; height: 22px; border-radius: 999px; padding: 2.5px; display: flex; align-items: center; }
    .sw-knob { width: 17px; height: 17px; border-radius: 50%; background: #FFFFFF; }
    `
  )
}

function renderThemesTrust() {
  const t = THEMES.dark
  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('home-outline', 17, t.textPrimary)}</div>`
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '发布端信任', rightBtn)}
    <div class="tt-body">
      <div class="tt-card" style="background:${t.bgSecondary};">
        <p class="tt-desc" style="color:${t.textSecondary};">在 Rodak 对照指纹，一致后退出远程控制并在本机触摸确认。</p>
        <div class="tt-row">
          <span style="color:${t.textTertiary};font-size:12.5px;">发布端指纹</span>
          <span style="color:${t.textSecondary};font-size:12.5px;">尚未信任</span>
        </div>
        <div class="tt-fp" style="color:${t.textPrimary};">
          <div>A3C1 F0E2 B4D6 A8C0</div>
          <div>E2F4 A6B8 C0D2 E4F6</div>
        </div>
      </div>
      <div class="tt-actions">
        <div class="tt-btn-sec" style="background:${t.bgTertiary};color:${t.textPrimary};">读取/刷新</div>
        <div class="tt-btn-pri" style="background:${t.primary};color:${t.onPrimary};">指纹一致，确认信任</div>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .tt-body { height: 202px; padding: 10px; display: flex; flex-direction: column; justify-content: space-between; }
    .tt-card { border-radius: 12px; padding: 11px 12px; display: flex; flex-direction: column; gap: 7px; }
    .tt-desc { font-size: 12.5px; line-height: 17px; }
    .tt-row { display: flex; align-items: center; justify-content: space-between; }
    .tt-fp { font-family: 'Cascadia Mono', 'Consolas', monospace; font-size: 13px; line-height: 19px; letter-spacing: 0.5px; }
    .tt-actions { display: grid; grid-template-columns: 96px 1fr; gap: 8px; }
    .tt-btn-sec, .tt-btn-pri { height: 38px; border-radius: 10px; display: flex; align-items: center; justify-content: center; font-size: 13.5px; font-weight: 500; }
    `
  )
}

function renderClock() {
  const t = THEMES.dark
  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('home-outline', 17, t.textPrimary)}</div>`
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, 'Clock', rightBtn)}
    <div class="ck-wrap">
      <div class="ck-face" style="background:${t.bgSecondary};">
        <div class="ck-time-row">
          <span class="ck-hm" style="color:${t.textPrimary};">10:08</span>
          <span class="ck-sec" style="color:${t.primary};">:26</span>
        </div>
        <div class="ck-date" style="color:${t.textSecondary};">Thu, 2026-11-26</div>
      </div>
      <div class="ck-card" style="background:${t.bgSecondary};color:${t.textPrimary};">
        ${ionSvg('globe-outline', 16, t.primary)}
        <span>Time zone: Asia/Shanghai (UTC+8)</span>
      </div>
      <div class="ck-card ck-srv" style="background:${t.bgSecondary};color:${t.textPrimary};">
        <span class="ck-srv-l">${ionSvg('cloud-outline', 16, t.primary)} Server: pool.ntp.org</span>
        <span class="ck-sync" style="background:${t.primary};">${ionSvg('refresh-outline', 16, t.onPrimary)}</span>
      </div>
      <div class="ck-foot" style="color:${t.success};">Synced</div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .ck-wrap { height: 202px; padding: 8px 10px 4px; display: flex; flex-direction: column; gap: 8px; }
    .ck-face { height: 90px; border-radius: 8px; padding: 12px 16px; display: flex; flex-direction: column; justify-content: space-between; }
    .ck-time-row { display: flex; align-items: baseline; gap: 4px; }
    .ck-hm { font-size: 32px; font-weight: 600; line-height: 34px; }
    .ck-sec { font-size: 18px; font-weight: 500; }
    .ck-date { font-size: 12px; }
    .ck-card { height: 34px; border-radius: 8px; padding: 0 10px; display: flex; align-items: center; gap: 10px; font-size: 12px; }
    .ck-srv { justify-content: space-between; }
    .ck-srv-l { display: inline-flex; align-items: center; gap: 10px; }
    .ck-sync { width: 40px; height: 26px; border-radius: 6px; display: flex; align-items: center; justify-content: center; }
    .ck-foot { text-align: center; font-size: 11px; margin-top: auto; }
    `
  )
}

function renderSettings() {
  const t = THEMES.dark
  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('home-outline', 17, t.textPrimary)}</div>`
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '设置', rightBtn)}
    <div class="st-body">
      <div class="st-cap" style="color:${t.textTertiary};">网络</div>
      <div class="st-group" style="background:${t.bgSecondary};">
        <div class="st-row" style="border-bottom:1px solid ${t.divider};">
          <span class="st-lbl" style="color:${t.textPrimary};">${ionSvg('wifi-outline', 15, t.primary)} Wi-Fi</span>
          <span class="st-val" style="color:${t.textSecondary};">书房-5G ${ionSvg('chevron-forward-outline', 14, t.textTertiary)}</span>
        </div>
        <div class="st-row">
          <span class="st-lbl" style="color:${t.textPrimary};">${ionSvg('link-outline', 15, t.primary)} 连接服务</span>
          <span class="st-val" style="color:${t.success};">已连接 ${ionSvg('chevron-forward-outline', 14, t.textTertiary)}</span>
        </div>
      </div>
      <div class="st-cap" style="color:${t.textTertiary};margin-top:6px;">显示</div>
      <div class="st-group" style="background:${t.bgSecondary};">
        <div class="st-row" style="border-bottom:1px solid ${t.divider};gap:8px;">
          ${ionSvg('sunny-outline', 15, t.primary)}
          <div class="st-track" style="background:${t.bgTertiary};"><span class="st-fill" style="width:75%;background:${t.primary};"></span></div>
          <span style="color:${t.textSecondary};font-size:11.5px;width:32px;text-align:right;">75%</span>
        </div>
        <div class="st-row">
          <span class="st-lbl" style="color:${t.textPrimary};">${ionSvg('color-palette-outline', 15, t.primary)} 主题</span>
          ${ionSvg('chevron-forward-outline', 14, t.textTertiary)}
        </div>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .st-body { padding: 4px 10px; }
    .st-cap { font-size: 11px; padding: 0 4px 3px; }
    .st-group { border-radius: 9px; overflow: hidden; }
    .st-row { height: 37px; padding: 0 10px; display: flex; align-items: center; justify-content: space-between; }
    .st-lbl { display: inline-flex; align-items: center; gap: 8px; font-size: 13px; }
    .st-val { display: inline-flex; align-items: center; gap: 4px; font-size: 12px; }
    .st-track { flex: 1; height: 6px; border-radius: 999px; overflow: hidden; }
    .st-fill { display: block; height: 100%; border-radius: 999px; }
    `
  )
}

function renderSystem() {
  const t = THEMES.dark
  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('home-outline', 17, t.textPrimary)}</div>`
  const items = [
    ['内部 SRAM', '312 KB 可用 (DMA 48 KB)'],
    ['外部 PSRAM', '6.4 MB / 8.0 MB'],
    ['Flash 主槽', 'ota_0 13.3 MB · SD OTA'],
    ['Wi-Fi', '书房-5G · -54 dBm']
  ]
  const rowsHtml = items
    .map(
      ([k, v], i) => `
      <div class="sy-row" style="${i < items.length - 1 ? `border-bottom:1px solid ${t.divider};` : ''}">
        <span style="color:${t.textTertiary};font-size:12px;">${k}</span>
        <span style="color:${t.textPrimary};font-size:12px;font-weight:500;">${v}</span>
      </div>`
    )
    .join('')
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '关于本机', rightBtn)}
    <div class="sy-body">
      <div class="sy-id" style="background:${t.bgSecondary};">
        <div>
          <div style="color:${t.textPrimary};font-size:14px;font-weight:600;">RYMCU BigSmart</div>
          <div style="color:${t.textTertiary};font-size:11.5px;margin-top:2px;">RodakOS v0.1.6 · ESP-IDF 6.0.2</div>
        </div>
        <span style="background:${t.bgTertiary};color:${t.success};padding:2px 8px;border-radius:999px;font-size:11px;font-weight:500;">Recovery OK</span>
      </div>
      <div class="sy-card" style="background:${t.bgSecondary};">${rowsHtml}</div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .sy-body { padding: 10px; display: flex; flex-direction: column; gap: 8px; }
    .sy-id { border-radius: 10px; padding: 10px 12px; display: flex; align-items: center; justify-content: space-between; }
    .sy-card { border-radius: 10px; overflow: hidden; }
    .sy-row { height: 32px; padding: 0 12px; display: flex; align-items: center; justify-content: space-between; }
    `
  )
}

function renderMusic() {
  const t = THEMES.dark
  const rightBtn = `<div class="hdr-btn" style="background:${t.bgTertiary};">${ionSvg('list-outline', 18, t.textPrimary)}</div>`
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '音乐', rightBtn)}
    <div class="mu-wrap">
      <div class="mu-cover" style="background:linear-gradient(180deg, ${t.slots.media}, ${darkenHex(t.slots.media, 31 / 255)});">
        ${ionSvg('musical-notes-outline', 38, '#FFFFFF')}
      </div>
      <div class="mu-info">
        <div class="mu-title" style="color:${t.textPrimary};">Midnight Lo-Fi</div>
        <div class="mu-sub" style="color:${t.textTertiary};">Rodak Demo · WAV</div>
        <div class="mu-chips">
          <span class="mu-chip" style="background:${t.bgSecondary};color:${t.textSecondary};">顺序</span>
          <span class="mu-chip" style="background:${t.bgSecondary};color:${t.textSecondary};">70%</span>
          <span class="mu-chip" style="background:${t.bgSecondary};color:${t.textSecondary};">1/2</span>
        </div>
      </div>
      <div class="mu-prog">
        <div class="mu-bar" style="background:${t.bgTertiary};"><span class="mu-fill" style="width:46%;background:${t.primary};"></span></div>
        <div class="mu-times" style="color:${t.textTertiary};"><span>1:18</span><span>2:49</span></div>
      </div>
      <div class="mu-ctrls">
        <div class="mu-btn" style="background:${t.bgSecondary};">${ionSvg('play-skip-back-outline', 18, t.textPrimary)}</div>
        <div class="mu-play" style="background:${t.primary};">${ionSvg('pause-outline', 22, t.onPrimary)}</div>
        <div class="mu-btn" style="background:${t.bgSecondary};">${ionSvg('play-skip-forward-outline', 18, t.textPrimary)}</div>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .mu-wrap { position: relative; height: 202px; }
    .mu-cover { position: absolute; left: 16px; top: 12px; width: 96px; height: 96px; border-radius: 12px; display: flex; align-items: center; justify-content: center; }
    .mu-info { position: absolute; left: 124px; top: 14px; width: 180px; }
    .mu-title { font-size: 15px; font-weight: 600; }
    .mu-sub { font-size: 11.5px; margin-top: 3px; }
    .mu-chips { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px; margin-top: 14px; }
    .mu-chip { height: 32px; border-radius: 8px; display: flex; align-items: center; justify-content: center; font-size: 11px; }
    .mu-prog { position: absolute; left: 16px; right: 16px; top: 118px; }
    .mu-bar { height: 5px; border-radius: 999px; overflow: hidden; }
    .mu-fill { display: block; height: 100%; border-radius: 999px; }
    .mu-times { display: flex; justify-content: space-between; font-size: 10.5px; margin-top: 3px; }
    .mu-ctrls { position: absolute; left: 0; right: 0; bottom: 6px; display: flex; align-items: center; justify-content: center; gap: 22px; }
    .mu-btn { width: 40px; height: 40px; border-radius: 50%; display: flex; align-items: center; justify-content: center; }
    .mu-play { width: 48px; height: 48px; border-radius: 50%; display: flex; align-items: center; justify-content: center; }
    `
  )
}

function renderFiles() {
  const t = THEMES.dark
  const dirs = [
    { name: '/photos', meta: '8 张图片', icon: 'folder-outline', col: t.primary },
    { name: '/music', meta: '2 首音频', icon: 'folder-outline', col: t.primary },
    { name: '/recordings', meta: '4 条录音', icon: 'folder-outline', col: t.primary },
    { name: '/rodakos', meta: '受保护系统目录', icon: 'lock-closed-outline', col: t.textTertiary }
  ]
  const rowsHtml = dirs
    .map(
      (d, i) => `
      <div class="fi-row" style="${i < dirs.length - 1 ? `border-bottom:1px solid ${t.divider};` : ''}">
        <div class="fi-left">
          ${ionSvg(d.icon, 18, d.col)}
          <span style="color:${t.textPrimary};font-size:13px;font-weight:500;">${d.name}</span>
        </div>
        <span style="color:${t.textTertiary};font-size:11.5px;">${d.meta}</span>
      </div>`
    )
    .join('')
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '文件')}
    <div class="fi-strip" style="color:${t.textTertiary};border-bottom:1px solid ${t.divider};">
      <span>/ · SDMMC FAT32</span>
      <span>1.4 / 32 GB</span>
    </div>
    <div class="fi-body"><div class="fi-card" style="background:${t.bgSecondary};">${rowsHtml}</div></div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .fi-strip { height: 24px; padding: 0 12px; display: flex; align-items: center; justify-content: space-between; font-size: 11.5px; }
    .fi-body { padding: 8px 10px; }
    .fi-card { border-radius: 10px; overflow: hidden; }
    .fi-row { height: 39px; padding: 0 12px; display: flex; align-items: center; justify-content: space-between; }
    .fi-left { display: inline-flex; align-items: center; gap: 9px; }
    `
  )
}

function renderPhotos() {
  const t = THEMES.dark
  const thumbs = [
    'linear-gradient(135deg, #0284c7, #1e293b)',
    'linear-gradient(135deg, #d97706, #451a03)',
    'linear-gradient(135deg, #059669, #064e3b)',
    'linear-gradient(135deg, #7c3aed, #1e1b4b)',
    'linear-gradient(135deg, #e11d48, #4c0519)',
    'linear-gradient(135deg, #0d9488, #134e4a)',
    'linear-gradient(135deg, #4f46e5, #1e1b4b)',
    'linear-gradient(135deg, #ca8a04, #422006)'
  ]
  const gridHtml = thumbs
    .map((bg) => `<div class="ph-tile" style="background:${bg};">${ionSvg('image-outline', 20, 'rgba(255,255,255,0.78)')}</div>`)
    .join('')
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '相册')}
    <div class="ph-grid">${gridHtml}</div>
    <div class="ph-bar" style="background:${t.bgSecondary};color:${t.textSecondary};">
      <span style="opacity:0.4;">上一页</span>
      <span>第 1 / 1 页 · 8 张</span>
      <span style="opacity:0.4;">下一页</span>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .ph-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 2px; padding: 2px; }
    .ph-tile { height: 78px; border-radius: 4px; display: flex; align-items: center; justify-content: center; }
    .ph-bar { height: 40px; padding: 0 14px; display: flex; align-items: center; justify-content: space-between; font-size: 12px; }
    `
  )
}

function renderCamera() {
  const t = THEMES.dark
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '相机')}
    <div class="cm-body">
      <div class="cm-view">
        <div class="cm-grid-h" style="top:33.3%;"></div>
        <div class="cm-grid-h" style="top:66.6%;"></div>
        <div class="cm-grid-v" style="left:33.3%;"></div>
        <div class="cm-grid-v" style="left:66.6%;"></div>
        <div class="cm-reticle"></div>
        <div class="cm-hud">GC0308 · 320×240 · #14</div>
      </div>
      <div class="cm-rail" style="background:${t.bgPrimary};">
        <span style="color:${t.success};font-size:9.5px;font-family:monospace;">DMA</span>
        <div class="cm-shut" style="border:3px solid ${t.textPrimary};"><div class="cm-shut-in" style="background:${t.textPrimary};"></div></div>
        <span style="color:${t.textTertiary};font-size:9.5px;">相册</span>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .cm-body { height: 202px; display: flex; }
    .cm-view { width: 266px; height: 202px; background: radial-gradient(circle at 46% 42%, #1e3a5f 0%, #0b1524 68%, #04070d 100%); position: relative; overflow: hidden; }
    .cm-grid-h { position: absolute; left: 0; right: 0; height: 1px; background: rgba(255,255,255,0.12); }
    .cm-grid-v { position: absolute; top: 0; bottom: 0; width: 1px; background: rgba(255,255,255,0.12); }
    .cm-reticle { position: absolute; left: 50%; top: 50%; width: 42px; height: 42px; margin: -21px 0 0 -21px; border: 1.5px solid #79CBFF; border-radius: 8px; }
    .cm-hud { position: absolute; left: 8px; bottom: 6px; font-size: 10px; color: #BED4E1; font-family: monospace; background: rgba(0,0,0,0.5); padding: 2px 6px; border-radius: 4px; }
    .cm-rail { width: 54px; height: 202px; padding: 16px 0; display: flex; flex-direction: column; align-items: center; justify-content: space-between; }
    .cm-shut { width: 44px; height: 44px; border-radius: 50%; display: flex; align-items: center; justify-content: center; }
    .cm-shut-in { width: 30px; height: 30px; border-radius: 50%; }
    `
  )
}

function renderMessages() {
  const t = THEMES.dark
  return pageWrap(
    t.bgPrimary,
    `
    ${appHeader(t, '消息中心')}
    <div class="ms-tabs" style="border-bottom:1px solid ${t.divider};">
      <span class="ms-tab" style="background:${t.primary};color:${t.onPrimary};">全部</span>
      <span class="ms-tab" style="background:${t.bgSecondary};color:${t.textSecondary};">待办</span>
      <span class="ms-tab" style="background:${t.bgSecondary};color:${t.textSecondary};">通知</span>
    </div>
    <div class="ms-body">
      <div class="ms-card" style="background:${t.bgSecondary};">
        <div class="ms-top">
          <span class="ms-left"><span class="ms-chip" style="background:rgba(242,124,124,0.2);color:${t.error};">待办</span><span class="ms-title" style="color:${t.textPrimary};">更换 3D 打印耗材</span></span>
          <span class="ms-time" style="color:${t.textTertiary};">10:02</span>
        </div>
        <div class="ms-sub" style="color:${t.textSecondary};">确认 PLA 料盘余量足够完成 6 件外壳与底座打印。</div>
      </div>
      <div class="ms-card" style="background:${t.bgSecondary};">
        <div class="ms-top">
          <span class="ms-left"><span class="ms-chip" style="background:rgba(143,182,200,0.2);color:${t.textTertiary};">通知</span><span class="ms-title" style="color:${t.textPrimary};">Rodak 工作台已连接</span></span>
          <span class="ms-time" style="color:${t.textTertiary};">09:45</span>
        </div>
        <div class="ms-sub" style="color:${t.textSecondary};">HTTPS / MQTTS / WSS 三通道已共享固定 .local 证书。</div>
      </div>
    </div>
    `,
    `
    ${COMMON_HEADER_CSS}
    .ms-tabs { height: 28px; padding: 0 10px; display: flex; align-items: center; gap: 6px; }
    .ms-tab { border-radius: 999px; padding: 2px 10px; font-size: 11.5px; font-weight: 500; }
    .ms-body { padding: 10px; display: flex; flex-direction: column; gap: 8px; }
    .ms-card { border-radius: 10px; padding: 9px 12px; display: flex; flex-direction: column; gap: 5px; }
    .ms-top { display: flex; align-items: center; justify-content: space-between; }
    .ms-left { display: inline-flex; align-items: center; gap: 6px; }
    .ms-chip { border-radius: 4px; padding: 1px 5px; font-size: 10.5px; font-weight: 500; }
    .ms-title { font-size: 13px; font-weight: 500; }
    .ms-time { font-size: 11px; }
    .ms-sub { font-size: 11.5px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
    `
  )
}

function renderControlCenter() {
  const t = THEMES.dark
  return pageWrap(
    t.bgPrimary,
    `
    <div class="cc-wrap">
      <div class="cc-top">
        <span style="color:${t.textPrimary};font-size:17px;font-weight:500;">Control Center</span>
        <span class="cc-close" style="background:${t.bgTertiary};">${ionSvg('close-outline', 16, t.textPrimary)}</span>
      </div>
      <div class="cc-wifi" style="background:${t.bgSecondary};color:${t.textPrimary};">
        ${ionSvg('wifi-outline', 18, t.textPrimary)}
        <span>Wi-Fi connected</span>
      </div>
      <div class="cc-br" style="background:${t.bgSecondary};color:${t.textPrimary};">
        <div class="cc-br-h">
          <span style="display:inline-flex;align-items:center;gap:8px;">${ionSvg('sunny-outline', 16, t.textPrimary)} Brightness</span>
          <span style="color:${t.textTertiary};font-size:12px;">75%</span>
        </div>
        <div class="cc-track" style="background:${t.bgTertiary};"><span class="cc-fill" style="width:75%;background:${t.primary};"></span></div>
      </div>
      <div class="cc-btns">
        <div class="cc-btn" style="background:${t.bgTertiary};color:${t.textPrimary};">${ionSvg('settings-outline', 16, t.textPrimary)} Settings</div>
        <div class="cc-btn" style="background:${t.bgTertiary};color:${t.textPrimary};">${ionSvg('lock-closed-outline', 16, t.textPrimary)} Lock</div>
      </div>
    </div>
    `,
    `
    .cc-wrap { position: relative; width: 320px; height: 240px; padding: 8px 14px 18px; display: flex; flex-direction: column; justify-content: space-between; }
    .cc-top { display: flex; align-items: center; justify-content: space-between; }
    .cc-close { width: 36px; height: 28px; border-radius: 6px; display: flex; align-items: center; justify-content: center; }
    .cc-wifi { height: 42px; border-radius: 8px; padding: 0 12px; display: flex; align-items: center; gap: 12px; font-size: 13.5px; }
    .cc-br { height: 68px; border-radius: 8px; padding: 8px 12px 12px; display: flex; flex-direction: column; justify-content: space-between; font-size: 13.5px; }
    .cc-br-h { display: flex; align-items: center; justify-content: space-between; }
    .cc-track { height: 8px; border-radius: 999px; overflow: hidden; }
    .cc-fill { display: block; height: 100%; border-radius: 999px; }
    .cc-btns { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
    .cc-btn { height: 48px; border-radius: 8px; display: flex; align-items: center; justify-content: center; gap: 8px; font-size: 13.5px; font-weight: 500; }
    `
  )
}

function renderLockScreen() {
  const t = THEMES.dark
  return pageWrap(
    t.bgPrimary,
    `
    <div class="lk-wrap">
      <div class="lk-top">
        ${ionSvg('lock-closed-outline', 17, t.textSecondary)}
        ${ionSvg('wifi-outline', 17, t.textSecondary)}
      </div>
      <div class="lk-mid">
        <div style="color:${t.textPrimary};font-size:28px;font-weight:600;line-height:32px;">10:08</div>
        <div style="color:${t.textSecondary};font-size:13.5px;margin-top:6px;">Thu, 2026-11-26</div>
        <div class="lk-pill" style="background:${t.bgSecondary};border:1px solid ${t.border};color:${t.textPrimary};">
          ${ionSvg('notifications-outline', 13, t.error)}
          <span>有 2 条新消息 · 1 项待办</span>
        </div>
      </div>
      <div class="lk-bot">
        ${ionSvg('arrow-up-outline', 17, t.textPrimary)}
        <span style="color:${t.textSecondary};font-size:11.5px;">Unlock</span>
      </div>
    </div>
    `,
    `
    .lk-wrap { width: 320px; height: 240px; padding: 12px 14px 10px; display: flex; flex-direction: column; justify-content: space-between; }
    .lk-top { display: flex; align-items: center; justify-content: space-between; }
    .lk-mid { display: flex; flex-direction: column; align-items: center; }
    .lk-pill { margin-top: 16px; height: 26px; padding: 0 12px; border-radius: 13px; display: inline-flex; align-items: center; gap: 6px; font-size: 11.5px; }
    .lk-bot { display: flex; flex-direction: column; align-items: center; gap: 2px; }
    `
  )
}

const SCREENS = [
  ['home-dark', () => renderHome('dark', { page: 1 })],
  ['home-light', () => renderHome('light', { page: 1 })],
  ['home-blue', () => renderHome('blue', { page: 1 })],
  ['home-green', () => renderHome('green', { page: 1 })],
  ['home-page2-dark', () => renderHome('dark', { page: 2 })],
  ['voice-caption-dark', () => renderHome('dark', { page: 1, voiceCaption: '已打开客厅主灯，亮度 60%' })],
  ['voice-caption-light', () => renderHome('light', { page: 1, voiceCaption: '已打开客厅主灯，亮度 60%' })],
  ['calendar-dark', () => renderCalendar('dark')],
  ['calendar-light', () => renderCalendar('light')],
  ['gyro-dark', () => renderGyro('dark')],
  ['gyro-light', () => renderGyro('light')],
  ['smart-dark', () => renderSmartDark()],
  ['smart-light', () => renderSmartLight()],
  ['themes-dark', () => renderThemes('dark')],
  ['themes-light', () => renderThemes('light')],
  ['themes-trust-dark', () => renderThemesTrust()],
  ['assistant-dark', () => renderAssistant()],
  ['assistant-thinking-dark', () => renderAssistantThinking()],
  ['recorder-dark', () => renderRecorder()],
  ['wol-dark', () => renderWol('dark')],
  ['wol-light', () => renderWol('light')],
  ['clock-dark', () => renderClock()],
  ['settings-dark', () => renderSettings()],
  ['system-dark', () => renderSystem()],
  ['music-dark', () => renderMusic()],
  ['files-dark', () => renderFiles()],
  ['photos-dark', () => renderPhotos()],
  ['camera-dark', () => renderCamera()],
  ['messages-dark', () => renderMessages()],
  ['control-center-dark', () => renderControlCenter()],
  ['lock-screen-dark', () => renderLockScreen()]
]

function renderOne(htmlPath, pngPath, profileDir) {
  return new Promise((resolve, reject) => {
    const fileUrl = `file:///${htmlPath.replace(/\\/g, '/')}`
    const proc = spawn(
      CHROME,
      [
        '--headless=new',
        '--disable-gpu',
        '--no-sandbox',
        '--no-first-run',
        '--no-default-browser-check',
        '--disable-extensions',
        '--disable-background-networking',
        '--hide-scrollbars',
        '--force-device-scale-factor=1',
        '--window-size=1280,960',
        `--user-data-dir=${profileDir}`,
        `--screenshot=${pngPath}`,
        fileUrl
      ],
      { stdio: 'ignore' }
    )
    const timer = setTimeout(() => {
      try {
        proc.kill('SIGKILL')
      } catch {}
      reject(new Error(`Chrome timed out for ${htmlPath}`))
    }, 30000)
    proc.on('exit', (code) => {
      clearTimeout(timer)
      if (code === 0 && fs.existsSync(pngPath)) resolve()
      else reject(new Error(`Chrome failed (${code}) for ${htmlPath}`))
    })
    proc.on('error', (err) => {
      clearTimeout(timer)
      reject(err)
    })
  })
}

async function main() {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'rodakos-hidpi-'))
  fs.mkdirSync(OUT_DIR, { recursive: true })
  const onlySet = process.argv.includes('--only-missing')
    ? new Set(
        SCREENS.map(([n]) => n).filter((n) => {
          const p = path.join(OUT_DIR, `${n}.webp`)
          return !fs.existsSync(p) || fs.statSync(p).size < 12000
        })
      )
    : null

  try {
    const selected = onlySet ? SCREENS.filter(([n]) => onlySet.has(n)) : SCREENS
    const jobs = selected.map(([name, fn]) => {
      const htmlPath = path.join(tmpDir, `${name}.html`)
      const pngPath = path.join(tmpDir, `${name}.png`)
      const profileDir = path.join(tmpDir, `prof-${name}`)
      fs.writeFileSync(htmlPath, fn(), 'utf8')
      return { name, htmlPath, pngPath, profileDir }
    })

    // Render 4 screens in parallel with isolated user-data-dir per job
    const queue = [...jobs]
    const workers = Array.from({ length: 4 }, async () => {
      while (queue.length > 0) {
        const job = queue.shift()
        await renderOne(job.htmlPath, job.pngPath, job.profileDir)
      }
    })
    await Promise.all(workers)

    // Convert 1280x960 PNGs -> crisp high-quality WebP via Python Pillow
    const pyScript = `
import sys
from pathlib import Path
from PIL import Image
tmp = Path(sys.argv[1])
out = Path(sys.argv[2])
count = 0
for png in sorted(tmp.glob('*.png')):
    with Image.open(png) as im:
        rgb = im.convert('RGB')
        dst = out / f'{png.stem}.webp'
        rgb.save(dst, format='WEBP', quality=95, method=5)
        print(f'[hidpi] {dst.name}: {rgb.width}x{rgb.height} ({dst.stat().st_size/1024:.1f} KiB)')
        count += 1
print(f'Rendered {count} crisp 4x Hi-DPI WebP screens to {out}')
`
    execFileSync('python', ['-c', pyScript, tmpDir, OUT_DIR], { stdio: 'inherit' })
  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true })
  }
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
