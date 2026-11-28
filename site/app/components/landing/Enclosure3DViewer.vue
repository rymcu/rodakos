<script setup lang="ts">
import type * as THREE from 'three'
import type { FirmwareThemeId, ScreenSceneId } from '~/types/landing'

type ViewPresetId = 'docked' | 'front' | 'left' | 'rear' | 'exploded'
type FilamentId = 'graphite' | 'ceramic' | 'blue' | 'orange'

const { locale } = useLandingCopy()
const { withBase } = useSiteAssetUrl()
const { firmwareTheme, boardLedColor, boardLedIntensity, lights } = useRodakDemoOs()

const canvasHost = useTemplateRef<HTMLDivElement>('canvasHost')
const activeView = shallowRef<ViewPresetId>('docked')
const activeFilament = shallowRef<FilamentId>('graphite')
const explode = shallowRef(0)
const loadState = shallowRef<'loading' | 'ready' | 'fallback'>('loading')

// Live 3D LCD interactive screen state (including 3D-rendered Shell Control Center & Lock Screen)
type ModelScreenId = ScreenSceneId | 'control-center' | 'lock-screen'
const screenScene = shallowRef<ModelScreenId>('home')
const prevAppBeforeShell = shallowRef<ScreenSceneId>('home')
const screenSleeping = shallowRef(false)
const assistantThinking = shallowRef(false)
const smartDetailMode = shallowRef(false)
const hoverHint = shallowRef('')
const isHoverClickable = shallowRef(false)
const isFullscreen = shallowRef(false)

const isEn = computed(() => locale.value === 'en')

const viewPresets = computed<{ id: ViewPresetId; label: string; icon: string }[]>(() =>
  isEn.value
    ? [
        { id: 'docked', label: '15° Cradle', icon: 'i-lucide-box' },
        { id: 'front', label: 'Front', icon: 'i-lucide-monitor' },
        { id: 'left', label: 'USB-C / TF', icon: 'i-lucide-usb' },
        { id: 'rear', label: 'Rear Audio/Cam', icon: 'i-lucide-camera' },
        { id: 'exploded', label: 'Exploded', icon: 'i-lucide-layers' }
      ]
    : [
        { id: 'docked', label: '15° 底座', icon: 'i-lucide-box' },
        { id: 'front', label: '正面交互', icon: 'i-lucide-monitor' },
        { id: 'left', label: '左侧接口', icon: 'i-lucide-usb' },
        { id: 'rear', label: '背面声学/镜头', icon: 'i-lucide-camera' },
        { id: 'exploded', label: '爆炸分解', icon: 'i-lucide-layers' }
      ]
)

const filaments = computed<
  {
    id: FilamentId
    label: string
    dotClass: string
    front: number
    rear: number
    cradle: number
    mid: number
    retainer: number
  }[]
>(() => [
  {
    id: 'graphite',
    label: isEn.value ? 'Graphite' : '哑光深灰',
    dotClass: 'bg-slate-600 ring-1 ring-slate-400',
    front: 0x334155,
    rear: 0x1e293b,
    cradle: 0x0f172a,
    mid: 0x0284c7,
    retainer: 0x475569
  },
  {
    id: 'ceramic',
    label: isEn.value ? 'Ceramic' : '陶瓷浅灰',
    dotClass: 'bg-slate-200 ring-1 ring-slate-400',
    front: 0xe2e8f0,
    rear: 0xcbd5e1,
    cradle: 0x334155,
    mid: 0x0284c7,
    retainer: 0x94a3b8
  },
  {
    id: 'blue',
    label: isEn.value ? 'CAD Blue' : '信号蓝',
    dotClass: 'bg-blue-600 ring-1 ring-sky-300',
    front: 0x2563eb,
    rear: 0x334155,
    cradle: 0x1e293b,
    mid: 0xf59e0b,
    retainer: 0x475569
  },
  {
    id: 'orange',
    label: isEn.value ? 'Orange' : '暖橙原型',
    dotClass: 'bg-orange-500 ring-1 ring-orange-300',
    front: 0xf97316,
    rear: 0x334155,
    cradle: 0x1e293b,
    mid: 0x38bdf8,
    retainer: 0x475569
  }
])

// Home Page 1 & Page 2 4x3 grid mapping matching exact 320x240 layout
const PAGE1_GRID: readonly { id: ScreenSceneId; label: string }[] = [
  { id: 'settings', label: 'Settings' },
  { id: 'photos', label: 'Photos' },
  { id: 'camera', label: 'Camera' },
  { id: 'clock', label: 'Clock' },
  { id: 'calendar', label: 'Calendar' },
  { id: 'files', label: 'Files' },
  { id: 'gyro', label: 'Gyro' },
  { id: 'system', label: 'System' },
  { id: 'music', label: 'Music' },
  { id: 'recorder', label: 'Recorder' },
  { id: 'assistant', label: 'Assistant' },
  { id: 'smart', label: 'Smart' }
]

const PAGE2_GRID: readonly { id: ScreenSceneId; label: string }[] = [
  { id: 'wol', label: 'Wake' },
  { id: 'themes', label: 'Themes' },
  { id: 'messages', label: 'Messages' }
]

function resolveTextureAsset(sceneId: ModelScreenId, theme: FirmwareThemeId): string {
  const isLight = theme === 'light'
  switch (sceneId) {
    case 'control-center':
      return 'screenshots/control-center-dark.webp'
    case 'lock-screen':
      return 'screenshots/lock-screen-dark.webp'
    case 'home':
      return `screenshots/home-${theme}.webp`
    case 'home-page2':
      return 'screenshots/home-page2-dark.webp'
    case 'voice-caption':
      return `screenshots/voice-caption-${isLight ? 'light' : 'dark'}.webp`
    case 'calendar':
      return `screenshots/calendar-${isLight ? 'light' : 'dark'}.webp`
    case 'gyro':
      return `screenshots/gyro-${isLight ? 'light' : 'dark'}.webp`
    case 'smart':
      return smartDetailMode.value || isLight
        ? 'screenshots/smart-light.webp'
        : 'screenshots/smart-dark.webp'
    case 'themes':
      return `screenshots/themes-${isLight ? 'light' : 'dark'}.webp`
    case 'themes-trust':
      return 'screenshots/themes-trust-dark.webp'
    case 'assistant':
      return assistantThinking.value
        ? 'screenshots/assistant-thinking-dark.webp'
        : 'screenshots/assistant-dark.webp'
    case 'recorder':
      return 'screenshots/recorder-dark.webp'
    case 'wol':
      return `screenshots/wol-${isLight ? 'light' : 'dark'}.webp`
    case 'clock':
      return 'screenshots/clock-dark.webp'
    case 'settings':
      return 'screenshots/settings-dark.webp'
    case 'system':
      return 'screenshots/system-dark.webp'
    case 'music':
      return 'screenshots/music-dark.webp'
    case 'files':
      return 'screenshots/files-dark.webp'
    case 'photos':
      return 'screenshots/photos-dark.webp'
    case 'camera':
      return 'screenshots/camera-dark.webp'
    case 'messages':
      return 'screenshots/messages-dark.webp'
  }
}

interface ScreenHitAction {
  hint: string
  run: () => void
}

function resolveScreenHit(px: number, py: number): ScreenHitAction | null {
  if (screenSleeping.value) {
    return {
      hint: isEn.value ? 'Tap to wake display' : '轻触唤醒屏幕',
      run: () => {
        screenSleeping.value = false
      }
    }
  }
  const sc = screenScene.value
  if (sc === 'lock-screen') {
    return {
      hint: isEn.value ? 'Tap Unlock on 3D LCD' : '点击解锁 3D 屏幕',
      run: () => {
        screenScene.value = prevAppBeforeShell.value
      }
    }
  }

  if (sc === 'control-center') {
    if (px >= 260 && py <= 46) {
      return {
        hint: isEn.value ? 'Close Control Center' : '关闭控制中心',
        run: () => {
          screenScene.value = prevAppBeforeShell.value
        }
      }
    }
    if (py >= 166) {
      if (px < 160) {
        return {
          hint: isEn.value ? 'Open Settings' : '打开 Settings 设置',
          run: () => {
            screenScene.value = 'settings'
          }
        }
      }
      return {
        hint: isEn.value ? 'Lock Screen' : '锁屏',
        run: () => {
          screenScene.value = 'lock-screen'
        }
      }
    }
    return {
      hint: isEn.value ? 'Close Control Center' : '关闭控制中心',
      run: () => {
        screenScene.value = prevAppBeforeShell.value
      }
    }
  }

  if (sc === 'home' || sc === 'voice-caption') {
    if (py >= 216) {
      return {
        hint: isEn.value ? 'Switch to Home Page 2' : '翻到桌面第 2 页',
        run: () => {
          screenScene.value = 'home-page2'
        }
      }
    }
    if (sc === 'voice-caption' && py >= 186) {
      return {
        hint: isEn.value ? 'Hide Voice Caption' : '轻触收起语音字幕',
        run: () => {
          screenScene.value = 'home'
        }
      }
    }
    if (px >= 12 && px <= 308 && py >= 26 && py <= 214) {
      const col = Math.min(3, Math.max(0, Math.floor((px - 14) / 74)))
      const row = Math.min(2, Math.max(0, Math.floor((py - 28) / 64)))
      const tile = PAGE1_GRID[row * 4 + col]
      if (tile) {
        return {
          hint: isEn.value ? `Open ${tile.label}` : `点击打开 ${tile.label}`,
          run: () => {
            screenScene.value = tile.id
          }
        }
      }
    }
    return null
  }

  if (sc === 'home-page2') {
    if (py >= 216) {
      return {
        hint: isEn.value ? 'Back to Home Page 1' : '返回桌面第 1 页',
        run: () => {
          screenScene.value = 'home'
        }
      }
    }
    if (px >= 12 && px <= 240 && py >= 26 && py <= 96) {
      const col = Math.floor((px - 14) / 74)
      const tile = PAGE2_GRID[col]
      if (tile) {
        return {
          hint: isEn.value ? `Open ${tile.label}` : `点击打开 ${tile.label}`,
          run: () => {
            screenScene.value = tile.id
          }
        }
      }
    }
    return null
  }

  if (px <= 66 && py <= 44) {
    return {
      hint: isEn.value ? 'Back to Home' : '返回桌面',
      run: () => {
        screenScene.value = sc === 'themes-trust' ? 'themes' : 'home'
      }
    }
  }

  if (sc === 'themes') {
    if (py >= 128 && py <= 178 && px >= 110) {
      const idx = Math.min(3, Math.max(0, Math.floor((px - 115) / 48)))
      const targetTheme = (['light', 'dark', 'blue', 'green'] as const)[idx] ?? 'dark'
      return {
        hint: isEn.value ? `Switch theme: ${targetTheme}` : `切换主题：${targetTheme}`,
        run: () => {
          firmwareTheme.value = targetTheme
        }
      }
    }
    if (py >= 182) {
      return {
        hint: isEn.value ? 'Open Publisher Trust' : '查看发布端物理触摸信任',
        run: () => {
          screenScene.value = 'themes-trust'
        }
      }
    }
  }

  if (sc === 'smart') {
    return {
      hint: isEn.value ? 'Toggle Smart view & LED1 color' : '切换单灯详情并联动 LED1 柔光',
      run: () => {
        smartDetailMode.value = !smartDetailMode.value
        const board = lights.value[0]
        if (board) {
          const palette = ['#FFDCA9', '#55B8FF', '#2CE0D0', '#42DC78', '#FF5252', '#C855FF']
          const nextIdx = (palette.indexOf(board.color) + 1) % palette.length
          board.on = true
          board.color = palette[nextIdx]!
        }
      }
    }
  }

  if (sc === 'assistant') {
    return {
      hint: isEn.value ? 'Simulate Wake / Thinking state' : '点击模拟唤醒「你好达克」思考态',
      run: () => {
        assistantThinking.value = !assistantThinking.value
      }
    }
  }

  return null
}

// Runtime Three.js handles (client only)
let THREE_REF: typeof THREE | null = null
let renderer: THREE.WebGLRenderer | null = null
let scene: THREE.Scene | null = null
let camera: THREE.PerspectiveCamera | null = null
let controls: {
  target: THREE.Vector3
  update: () => void
  dispose: () => void
} | null = null
let resizeObserver: ResizeObserver | null = null
let rafId = 0
let targetExplode = 0
let currentExplode = 0

let partObjects: Record<string, THREE.Object3D> = {}
let partMaterials: Record<string, THREE.MeshStandardMaterial> = {}
let lcdScreenMesh: THREE.Mesh | null = null
let lcdScreenMat: THREE.MeshBasicMaterial | null = null
let rgbHaloMat: THREE.MeshBasicMaterial | null = null
const switchMeshes: {
  id: 'power' | 'boot' | 'io10'
  mesh: THREE.Mesh
  hitMesh: THREE.Mesh
  baseZ: number
  press: number
}[] = []
const textureCache = new Map<string, THREE.Texture>()
let texLoader: THREE.TextureLoader | null = null

let cameraLerp: {
  pos: THREE.Vector3
  target: THREE.Vector3
  active: boolean
} | null = null

// Camera presets tuned so exploded 9-layer Z-stack sits squarely centered in view
const CAMERA_PRESETS: Record<
  ViewPresetId,
  { pos: [number, number, number]; target: [number, number, number]; explode?: number }
> = {
  docked: { pos: [1.35, 0.92, 1.85], target: [0.0, 0.44, 0.04], explode: 0 },
  front: { pos: [0.0, 0.52, 1.62], target: [0.0, 0.45, 0.14], explode: 0 },
  left: { pos: [-1.85, 0.56, 0.45], target: [-0.2, 0.46, 0.06], explode: 0 },
  rear: { pos: [-0.45, 0.68, -1.95], target: [0.0, 0.46, 0.02], explode: 0 },
  exploded: { pos: [2.95, 1.15, 1.55], target: [0.0, 0.42, 0.12], explode: 100 }
}

async function applyScreenTexture(sceneId: ModelScreenId, theme: FirmwareThemeId): Promise<void> {
  if (!THREE_REF || !lcdScreenMat || !texLoader || !renderer) return
  if (screenSleeping.value) {
    lcdScreenMat.map = null
    lcdScreenMat.color.setHex(0x06090e)
    lcdScreenMat.needsUpdate = true
    return
  }
  lcdScreenMat.color.setHex(0xffffff)
  const rel = resolveTextureAsset(sceneId, theme)
  let tex = textureCache.get(rel)
  if (!tex) {
    try {
      tex = await texLoader.loadAsync(withBase(rel))
      tex.colorSpace = THREE_REF.SRGBColorSpace
      tex.magFilter = THREE_REF.LinearFilter
      tex.minFilter = THREE_REF.LinearMipmapLinearFilter
      tex.anisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy())
      textureCache.set(rel, tex)
    } catch {
      return
    }
  }
  if (lcdScreenMat) {
    lcdScreenMat.map = tex
    lcdScreenMat.needsUpdate = true
  }
}

function updateRgbHaloMaterial(): void {
  if (!rgbHaloMat) return
  if (screenSleeping.value || boardLedColor.value === 'transparent') {
    rgbHaloMat.opacity = 0
  } else {
    rgbHaloMat.color.set(boardLedColor.value)
    rgbHaloMat.opacity = Math.min(0.82, Math.max(0.35, boardLedIntensity.value))
  }
}

watch([screenScene, screenSleeping, firmwareTheme, assistantThinking, smartDetailMode], () => {
  void applyScreenTexture(screenScene.value, firmwareTheme.value)
  updateRgbHaloMaterial()
})

watch([boardLedColor, boardLedIntensity], () => {
  updateRgbHaloMaterial()
})

function selectPreset(presetId: ViewPresetId): void {
  activeView.value = presetId
  const cfg = CAMERA_PRESETS[presetId]
  if (cfg.explode !== undefined) {
    explode.value = cfg.explode
    targetExplode = cfg.explode / 100
  }
  if (cameraLerp) {
    cameraLerp.pos.set(...cfg.pos)
    cameraLerp.target.set(...cfg.target)
    cameraLerp.active = true
  }
}

function applyFilamentColors(id: FilamentId): void {
  activeFilament.value = id
  const palette = filaments.value.find((f) => f.id === id) ?? filaments.value[0]!
  partMaterials.front_shell?.color.setHex(palette.front)
  partMaterials.rear_shell?.color.setHex(palette.rear)
  partMaterials.desktop_cradle?.color.setHex(palette.cradle)
  partMaterials.mid_frame?.color.setHex(palette.mid)
  partMaterials.rear_retainer?.color.setHex(palette.retainer)
  partMaterials.camera_clamp?.color.setHex(palette.retainer)
}

// Authentic BigSmart hardware key actions matching button_binding_service.cc & board_peripherals.yaml:
// - KEY_POWER (STC8G1K08A MCU): hardware sleep/wake
// - IO0 BOOT: single click -> Home
// - IO10 KEY: single click -> open fullscreen Control Center / double-click -> Smart
let io10ClickTimer: ReturnType<typeof setTimeout> | null = null

function triggerPhysicalSwitch(id: 'power' | 'boot' | 'io10'): void {
  const sw = switchMeshes.find((s) => s.id === id)
  if (sw) sw.press = 1

  if (id === 'power') {
    screenSleeping.value = !screenSleeping.value
    return
  }
  if (screenSleeping.value) {
    screenSleeping.value = false
    return
  }
  if (id === 'boot') {
    screenScene.value = 'home'
    return
  }
  // IO10: 280 ms single vs double click window matching kSingleClickDelayTicks
  // Single click -> Control Center right on the 3D LCD; Double click -> Smart right on the 3D LCD
  if (io10ClickTimer) {
    clearTimeout(io10ClickTimer)
    io10ClickTimer = null
    screenScene.value = 'smart'
  } else {
    io10ClickTimer = setTimeout(() => {
      io10ClickTimer = null
      if (screenScene.value === 'control-center') {
        screenScene.value = prevAppBeforeShell.value
      } else if (screenScene.value !== 'lock-screen') {
        prevAppBeforeShell.value = screenScene.value
        screenScene.value = 'control-center'
      }
    }, 280)
  }
}

function toggleFullscreen(): void {
  isFullscreen.value = !isFullscreen.value
}

function handleFullscreenKeydown(ev: KeyboardEvent): void {
  if (ev.key === 'Escape' && isFullscreen.value) {
    isFullscreen.value = false
  }
}

function syncRendererSize(): void {
  const host = canvasHost.value
  if (!host || !renderer || !camera) return
  const rect = host.getBoundingClientRect()
  const w = Math.max(Math.round(rect.width), 280)
  const h = Math.max(Math.round(rect.height), 260)
  camera.aspect = w / h
  camera.updateProjectionMatrix()
  renderer.setSize(w, h)
}

watch(isFullscreen, async (open) => {
  if (!import.meta.client) return
  if (open) {
    document.body.style.overflow = 'hidden'
    window.addEventListener('keydown', handleFullscreenKeydown)
  } else {
    document.body.style.overflow = ''
    window.removeEventListener('keydown', handleFullscreenKeydown)
  }
  await nextTick()
  syncRendererSize()
  window.requestAnimationFrame(() => syncRendererSize())
})

watch(explode, (val) => {
  targetExplode = Math.max(0, Math.min(1, val / 100))
})

let cleanupPointerListeners: (() => void) | null = null

onMounted(async () => {
  const host = canvasHost.value
  if (!host) return

  try {
    const [
      THREE_MOD,
      { OrbitControls },
      { GLTFLoader },
      { RoomEnvironment },
      { toCreasedNormals }
    ] = await Promise.all([
      import('three'),
      import('three/examples/jsm/controls/OrbitControls.js'),
      import('three/examples/jsm/loaders/GLTFLoader.js'),
      import('three/examples/jsm/environments/RoomEnvironment.js'),
      import('three/examples/jsm/utils/BufferGeometryUtils.js')
    ])
    THREE_REF = THREE_MOD

    const width = Math.max(host.clientWidth, 320)
    const height = Math.max(host.clientHeight, 300)

    renderer = new THREE_MOD.WebGLRenderer({
      antialias: true,
      alpha: true,
      powerPreference: 'high-performance'
    })
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2))
    renderer.setSize(width, height)
    renderer.toneMapping = THREE_MOD.ACESFilmicToneMapping
    renderer.toneMappingExposure = 1.08
    renderer.outputColorSpace = THREE_MOD.SRGBColorSpace
    renderer.shadowMap.enabled = true
    renderer.shadowMap.type = THREE_MOD.PCFSoftShadowMap
    renderer.domElement.style.display = 'block'
    host.appendChild(renderer.domElement)

    scene = new THREE_MOD.Scene()

    const pmrem = new THREE_MOD.PMREMGenerator(renderer)
    scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture
    pmrem.dispose()

    camera = new THREE_MOD.PerspectiveCamera(34, width / height, 0.05, 30)
    camera.position.set(...CAMERA_PRESETS.docked.pos)

    const orbit = new OrbitControls(camera, renderer.domElement)
    orbit.enableDamping = true
    orbit.dampingFactor = 0.07
    orbit.enablePan = false
    orbit.minDistance = 1.05
    orbit.maxDistance = 4.6
    orbit.target.set(...CAMERA_PRESETS.docked.target)
    orbit.addEventListener('start', () => {
      if (cameraLerp) cameraLerp.active = false
    })
    controls = orbit

    cameraLerp = {
      pos: new THREE_MOD.Vector3(...CAMERA_PRESETS.docked.pos),
      target: new THREE_MOD.Vector3(...CAMERA_PRESETS.docked.target),
      active: false
    }

    // Studio three-point lighting
    const hemi = new THREE_MOD.HemisphereLight(0xe0f2fe, 0x0f172a, 0.65)
    scene.add(hemi)

    const keyLight = new THREE_MOD.DirectionalLight(0xffffff, 1.45)
    keyLight.position.set(2.4, 3.2, 2.8)
    keyLight.castShadow = true
    keyLight.shadow.mapSize.set(1024, 1024)
    keyLight.shadow.camera.near = 0.5
    keyLight.shadow.camera.far = 10
    const d = 1.8
    keyLight.shadow.camera.left = -d
    keyLight.shadow.camera.right = d
    keyLight.shadow.camera.top = d
    keyLight.shadow.camera.bottom = -d
    keyLight.shadow.bias = -0.0005
    scene.add(keyLight)

    const rimLight = new THREE_MOD.DirectionalLight(0x7dd3fc, 0.75)
    rimLight.position.set(-2.5, 1.4, -2.2)
    scene.add(rimLight)

    const ground = new THREE_MOD.Mesh(
      new THREE_MOD.PlaneGeometry(6, 6),
      new THREE_MOD.ShadowMaterial({ opacity: 0.24 })
    )
    ground.rotation.x = -Math.PI / 2
    ground.position.y = -0.002
    ground.receiveShadow = true
    scene.add(ground)

    const assemblyRoot = new THREE_MOD.Group()
    scene.add(assemblyRoot)

    const handsetGroup = new THREE_MOD.Group()
    handsetGroup.position.set(0, 0.032, 0)
    handsetGroup.rotation.x = THREE_MOD.MathUtils.degToRad(-15)
    assemblyRoot.add(handsetGroup)

    // Load 6 real parametric CAD parts from rodakos-enclosure.glb
    const loader = new GLTFLoader()
    const gltf = await loader.loadAsync(withBase('hardware/rodakos-enclosure.glb'))

    const initialPalette = filaments.value[0]!
    const colorByPart: Record<string, number> = {
      front_shell: initialPalette.front,
      rear_shell: initialPalette.rear,
      desktop_cradle: initialPalette.cradle,
      mid_frame: initialPalette.mid,
      rear_retainer: initialPalette.retainer,
      camera_clamp: initialPalette.retainer
    }

    const loadedMeshes: THREE.Mesh[] = []
    gltf.scene.traverse((obj) => {
      if ((obj as THREE.Mesh).isMesh) {
        loadedMeshes.push(obj as THREE.Mesh)
      }
    })

    for (const mesh of loadedMeshes) {
      const name = mesh.name
      const creased = toCreasedNormals(mesh.geometry, Math.PI / 5)
      mesh.geometry.dispose()
      mesh.geometry = creased

      const mat = new THREE_MOD.MeshStandardMaterial({
        color: colorByPart[name] ?? 0x334155,
        roughness: name === 'desktop_cradle' ? 0.62 : 0.48,
        metalness: 0.05
      })
      mesh.material = mat
      mesh.castShadow = true
      mesh.receiveShadow = true
      partMaterials[name] = mat

      if (name === 'desktop_cradle') {
        assemblyRoot.add(mesh)
      } else {
        handsetGroup.add(mesh)
      }
      partObjects[name] = mesh
    }

    // =========================================================================
    // INTERNAL HARDWARE SUB-GROUPS IN EXACT 3d/enclosure/params.py + verify.py
    // PHYSICAL Z-ORDER (Front +Z -> Rear -Z, 1 scene unit = 100 mm):
    //   1. front_shell          Z=[12.0, 33.2]   explode +0.48
    //   2. screenGroup          Z=[27.64, 31.2]  explode +0.24
    //   2b. micsKeysGroup       Z=[25.1, 34.7]   explode +0.24
    //   3. mid_frame            Z=[19.8, 27.6]   explode +0.12
    //   4. mainboardGroup       Z=[15.04, 19.8]  explode +0.02
    //   5. rear_retainer        Z=[12.1, 13.7]   explode -0.06
    //   6. camera_clamp         Z=[6.14, 7.74]   explode -0.08
    //   7. rearModulesGroup     Z=[2.2, 13.7]    explode -0.19 (104050 battery + 3525 speaker)
    //   8. cameraGroup          Z=[0.4, 6.14]    explode -0.21 (GC0308 module + lens)
    //   9. rear_shell           Z=[0.0, 12.0]    explode -0.36
    // =========================================================================

    // --- Layer 2: Screen assembly (66.95 x 47.80 x 3.56 mm, Z=[27.64, 31.20]) ---
    const screenGroup = new THREE_MOD.Group()
    handsetGroup.add(screenGroup)
    partObjects.hw_screen = screenGroup

    texLoader = new THREE_MOD.TextureLoader()
    lcdScreenMat = new THREE_MOD.MeshBasicMaterial({
      color: 0xffffff,
      toneMapped: false
    })

    const lcdBezel = new THREE_MOD.Mesh(
      new THREE_MOD.BoxGeometry(0.6695, 0.478, 0.0356),
      new THREE_MOD.MeshStandardMaterial({ color: 0x090d14, roughness: 0.28, metalness: 0.12 })
    )
    // Asymmetric screen housing: wide right bezel (+6.92 mm) vs narrow left bezel (-2.43 mm) -> X=+2.245 mm
    lcdBezel.position.set(0.02245, 0.475, 0.2942)
    lcdBezel.castShadow = true
    screenGroup.add(lcdBezel)

    lcdScreenMesh = new THREE_MOD.Mesh(new THREE_MOD.PlaneGeometry(0.576, 0.432), lcdScreenMat)
    lcdScreenMesh.position.set(0, 0.475, 0.3123)
    lcdScreenMesh.name = 'interactive_lcd'
    screenGroup.add(lcdScreenMesh)

    const lcdGlass = new THREE_MOD.Mesh(
      new THREE_MOD.PlaneGeometry(0.576, 0.432),
      new THREE_MOD.MeshStandardMaterial({
        color: 0xffffff,
        transparent: true,
        opacity: 0.04,
        roughness: 0.08,
        metalness: 0.9,
        depthWrite: false
      })
    )
    lcdGlass.position.set(0, 0.475, 0.3131)
    screenGroup.add(lcdGlass)

    await applyScreenTexture(screenScene.value, firmwareTheme.value)

    // --- Layer 2b: mics-keys PCB (83.975 x 9.975 x 1.60 mm, Z=[25.10, 26.70]) + 3 switches + 2 MEMS mics + LED1 ---
    const micsKeysGroup = new THREE_MOD.Group()
    handsetGroup.add(micsKeysGroup)
    partObjects.hw_mics_keys = micsKeysGroup

    const mkPcb = new THREE_MOD.Mesh(
      new THREE_MOD.BoxGeometry(0.8398, 0.0998, 0.016),
      new THREE_MOD.MeshStandardMaterial({ color: 0x166534, roughness: 0.45, metalness: 0.2 })
    )
    mkPcb.position.set(0, 0.115, 0.259)
    mkPcb.castShadow = true
    micsKeysGroup.add(mkPcb)

    const switchBaseMat = new THREE_MOD.MeshStandardMaterial({
      color: 0x1e293b,
      roughness: 0.45,
      metalness: 0.35
    })
    const switchStemMat = new THREE_MOD.MeshStandardMaterial({
      color: 0x0b0f17,
      roughness: 0.4,
      metalness: 0.15
    })
    const swSpecs: readonly { id: 'power' | 'boot' | 'io10'; x: number }[] = [
      { id: 'power', x: -0.21 }, // SW2
      { id: 'boot', x: -0.09 }, // SW1
      { id: 'io10', x: 0.03 } // SW3
    ]
    const switchHitMat = new THREE_MOD.MeshBasicMaterial({
      transparent: true,
      opacity: 0,
      depthWrite: false
    })
    switchMeshes.length = 0
    for (const sw of swSpecs) {
      const base = new THREE_MOD.Mesh(new THREE_MOD.BoxGeometry(0.06, 0.06, 0.034), switchBaseMat)
      base.position.set(sw.x, 0.115, 0.284)
      micsKeysGroup.add(base)

      const stem = new THREE_MOD.Mesh(new THREE_MOD.CylinderGeometry(0.018, 0.018, 0.06, 24), switchStemMat)
      stem.rotation.x = Math.PI / 2
      stem.position.set(sw.x, 0.115, 0.317) // Z_max = 34.7mm (1.5mm proud of Z=33.2)
      stem.castShadow = true
      stem.name = `switch_${sw.id}`
      micsKeysGroup.add(stem)

      const hitMesh = new THREE_MOD.Mesh(
        new THREE_MOD.CylinderGeometry(0.038, 0.038, 0.075, 16),
        switchHitMat
      )
      hitMesh.rotation.x = Math.PI / 2
      hitMesh.position.set(sw.x, 0.115, 0.32)
      hitMesh.name = `switch_hit_${sw.id}`
      micsKeysGroup.add(hitMesh)

      switchMeshes.push({ id: sw.id, mesh: stem, hitMesh, baseZ: 0.317, press: 0 })
    }

    const micGoldMat = new THREE_MOD.MeshStandardMaterial({
      color: 0xd4af37,
      roughness: 0.3,
      metalness: 0.85
    })
    for (const micX of [-0.33, 0.33]) {
      const micCan = new THREE_MOD.Mesh(new THREE_MOD.BoxGeometry(0.028, 0.038, 0.01), micGoldMat)
      micCan.position.set(micX, 0.115, 0.272)
      micsKeysGroup.add(micCan)
    }

    const led5050 = new THREE_MOD.Mesh(
      new THREE_MOD.BoxGeometry(0.05, 0.054, 0.016),
      new THREE_MOD.MeshStandardMaterial({ color: 0xf8fafc, roughness: 0.3, metalness: 0.1 })
    )
    led5050.position.set(0.37, 0.115, 0.275)
    micsKeysGroup.add(led5050)

    // Seamless diffused RGB glow on front_shell outer membrane (moves with front_shell)
    rgbHaloMat = new THREE_MOD.MeshBasicMaterial({
      color: 0x38bdf8,
      transparent: true,
      opacity: 0.65,
      toneMapped: false
    })
    const rgbHalo = new THREE_MOD.Mesh(new THREE_MOD.CircleGeometry(0.028, 24), rgbHaloMat)
    rgbHalo.position.set(0.37, 0.115, 0.3326)
    partObjects.front_shell?.add(rgbHalo)
    updateRgbHaloMaterial()

    // --- Layer 4: MainBoard V6.0 PCB (Z=[18.2, 19.8]) + ESP32-S3 shield + USB-C + TF slot ---
    const mainboardGroup = new THREE_MOD.Group()
    handsetGroup.add(mainboardGroup)
    partObjects.hw_mainboard = mainboardGroup

    const pcbMesh = new THREE_MOD.Mesh(
      new THREE_MOD.BoxGeometry(0.6998, 0.4998, 0.016),
      new THREE_MOD.MeshStandardMaterial({ color: 0x15803d, roughness: 0.45, metalness: 0.25 })
    )
    pcbMesh.position.set(-0.148, 0.475, 0.19)
    pcbMesh.castShadow = true
    mainboardGroup.add(pcbMesh)

    // ESP32-S3-WROOM-1 RF metal shield facing -Z (Z=[15.09, 18.2])
    const espShield = new THREE_MOD.Mesh(
      new THREE_MOD.BoxGeometry(0.18, 0.255, 0.031),
      new THREE_MOD.MeshStandardMaterial({ color: 0xcbd5e1, roughness: 0.28, metalness: 0.85 })
    )
    espShield.position.set(-0.08, 0.48, 0.1665)
    mainboardGroup.add(espShield)

    // Left-wall USB-C receptacle (X=-48.5, Y=60.0, Z=16.62) & TF card slot (X=-45.5, Y=37.5, Z=17.14)
    const metalPortMat = new THREE_MOD.MeshStandardMaterial({
      color: 0xdbeafe,
      roughness: 0.24,
      metalness: 0.88
    })
    const usbcMetal = new THREE_MOD.Mesh(new THREE_MOD.BoxGeometry(0.075, 0.09, 0.032), metalPortMat)
    usbcMetal.position.set(-0.468, 0.6, 0.1662)
    mainboardGroup.add(usbcMetal)

    const tfSlot = new THREE_MOD.Mesh(new THREE_MOD.BoxGeometry(0.115, 0.115, 0.018), metalPortMat)
    tfSlot.position.set(-0.442, 0.375, 0.1714)
    mainboardGroup.add(tfSlot)

    // --- Layer 7: Rear modules inside rear_shell bays (104050 LiPo Battery + 3525 Enclosed Speaker, Z_min=2.2mm) ---
    const rearModulesGroup = new THREE_MOD.Group()
    handsetGroup.add(rearModulesGroup)
    partObjects.hw_rear_modules = rearModulesGroup

    // 104050 LiPo pack: X=[-21.5, 20.5] (42mm), Y=[11.5, 63.5] (52mm), Z=[2.2, 13.7] (11.5mm -> ctr 7.95mm)
    const batteryMesh = new THREE_MOD.Mesh(
      new THREE_MOD.BoxGeometry(0.42, 0.52, 0.115),
      new THREE_MOD.MeshStandardMaterial({ color: 0x94a3b8, roughness: 0.32, metalness: 0.58 })
    )
    batteryMesh.position.set(-0.005, 0.375, 0.0795)
    batteryMesh.castShadow = true
    rearModulesGroup.add(batteryMesh)

    const kaptonStrip = new THREE_MOD.Mesh(
      new THREE_MOD.BoxGeometry(0.422, 0.055, 0.117),
      new THREE_MOD.MeshStandardMaterial({ color: 0xd97706, roughness: 0.35, metalness: 0.15 })
    )
    kaptonStrip.position.set(-0.005, 0.608, 0.0795)
    rearModulesGroup.add(kaptonStrip)

    // 3525 enclosed speaker: X=[23.0, 48.4] (25.4mm, ctr 35.7), Y=[23.0, 58.4] (35.4mm, ctr 40.7), Z=[2.2, 8.9] (6.7mm, ctr 5.55)
    const spkBox = new THREE_MOD.Mesh(
      new THREE_MOD.BoxGeometry(0.254, 0.354, 0.067),
      new THREE_MOD.MeshStandardMaterial({ color: 0x18181b, roughness: 0.72, metalness: 0.1 })
    )
    spkBox.position.set(0.357, 0.407, 0.0555)
    spkBox.castShadow = true
    rearModulesGroup.add(spkBox)

    // Oval speaker driver cone facing -Z toward rear_shell acoustic window
    const spkCone = new THREE_MOD.Mesh(
      new THREE_MOD.CircleGeometry(0.088, 28),
      new THREE_MOD.MeshStandardMaterial({ color: 0x334155, roughness: 0.55, metalness: 0.25 })
    )
    spkCone.scale.set(0.88, 1.32, 1)
    spkCone.rotation.y = Math.PI
    spkCone.position.set(0.357, 0.407, 0.0215)
    rearModulesGroup.add(spkCone)

    // --- Layer 8: Rear GC0308 Camera module (X=7.0, Y=77.5, body Z=[1.2, 6.14], lens barrel Z=[0.4, 1.2]) ---
    const cameraGroup = new THREE_MOD.Group()
    handsetGroup.add(cameraGroup)
    partObjects.hw_camera = cameraGroup

    const camBody = new THREE_MOD.Mesh(
      new THREE_MOD.BoxGeometry(0.082, 0.082, 0.0494),
      new THREE_MOD.MeshStandardMaterial({ color: 0x0f172a, roughness: 0.38, metalness: 0.45 })
    )
    camBody.position.set(0.07, 0.775, 0.0367)
    cameraGroup.add(camBody)

    const camBarrel = new THREE_MOD.Mesh(
      new THREE_MOD.CylinderGeometry(0.035, 0.035, 0.012, 24),
      new THREE_MOD.MeshStandardMaterial({ color: 0x0f172a, roughness: 0.2, metalness: 0.65 })
    )
    camBarrel.rotation.x = Math.PI / 2
    camBarrel.position.set(0.07, 0.775, 0.008)
    cameraGroup.add(camBarrel)

    const camGlass = new THREE_MOD.Mesh(
      new THREE_MOD.CircleGeometry(0.023, 24),
      new THREE_MOD.MeshStandardMaterial({ color: 0x1d4ed8, roughness: 0.08, metalness: 0.85 })
    )
    camGlass.rotation.y = Math.PI
    camGlass.position.set(0.07, 0.775, 0.0018)
    cameraGroup.add(camGlass)

    // 6. Three.js Raycaster for direct 3D screen & hardware button interaction
    const raycaster = new THREE_MOD.Raycaster()
    const pointerNdc = new THREE_MOD.Vector2()
    let downX = 0
    let downY = 0
    const domEl = renderer.domElement

    const inspectPointer = (ev: PointerEvent): ScreenHitAction | null => {
      if (!camera || !lcdScreenMesh) return null
      const rect = domEl.getBoundingClientRect()
      if (rect.width <= 0 || rect.height <= 0) return null
      pointerNdc.x = ((ev.clientX - rect.left) / rect.width) * 2 - 1
      pointerNdc.y = -((ev.clientY - rect.top) / rect.height) * 2 + 1
      raycaster.setFromCamera(pointerNdc, camera)

      const targets: THREE.Object3D[] = [lcdScreenMesh, ...switchMeshes.map((s) => s.hitMesh)]
      const hits = raycaster.intersectObjects(targets, false)
      const hit = hits[0]
      if (!hit) return null

      if (hit.object === lcdScreenMesh && hit.uv) {
        const px = hit.uv.x * 320
        const py = (1 - hit.uv.y) * 240
        return resolveScreenHit(px, py)
      }

      const sw = switchMeshes.find((s) => s.hitMesh === hit.object)
      if (sw) {
        const labels: Record<'power' | 'boot' | 'io10', string> = isEn.value
          ? {
              power: 'POWER (STC8G MCU) · Sleep / Wake Display',
              boot: 'IO0 BOOT · Single Click -> Home',
              io10: 'IO10 KEY · Single: Control Center / Double: Smart'
            }
          : {
              power: 'POWER 电源键 (STC8G) · 息屏/亮屏',
              boot: 'IO0 BOOT 键 · 单击回到桌面',
              io10: 'IO10 侧键 · 单击控制中心 / 双击打开 Smart'
            }
        return {
          hint: labels[sw.id],
          run: () => triggerPhysicalSwitch(sw.id)
        }
      }
      return null
    }

    const onPointerDown = (ev: PointerEvent) => {
      downX = ev.clientX
      downY = ev.clientY
    }

    const onPointerMove = (ev: PointerEvent) => {
      if (ev.buttons !== 0) {
        isHoverClickable.value = false
        hoverHint.value = ''
        return
      }
      const action = inspectPointer(ev)
      isHoverClickable.value = Boolean(action)
      hoverHint.value = action?.hint ?? ''
    }

    const onPointerUp = (ev: PointerEvent) => {
      if (Math.hypot(ev.clientX - downX, ev.clientY - downY) > 6) return
      const action = inspectPointer(ev)
      if (action) {
        action.run()
      }
    }

    const onPointerLeave = () => {
      isHoverClickable.value = false
      hoverHint.value = ''
    }

    domEl.addEventListener('pointerdown', onPointerDown)
    domEl.addEventListener('pointermove', onPointerMove)
    domEl.addEventListener('pointerup', onPointerUp)
    domEl.addEventListener('pointerleave', onPointerLeave)
    cleanupPointerListeners = () => {
      domEl.removeEventListener('pointerdown', onPointerDown)
      domEl.removeEventListener('pointermove', onPointerMove)
      domEl.removeEventListener('pointerup', onPointerUp)
      domEl.removeEventListener('pointerleave', onPointerLeave)
    }

    loadState.value = 'ready'

    resizeObserver = new ResizeObserver(() => {
      syncRendererSize()
    })
    resizeObserver.observe(host)

    const animate = () => {
      rafId = window.requestAnimationFrame(animate)

      if (Math.abs(currentExplode - targetExplode) > 0.0005) {
        currentExplode += (targetExplode - currentExplode) * 0.12
      } else {
        currentExplode = targetExplode
      }

      // Strictly monotonic Front (+Z) -> Rear (-Z) exploded Z-stack matching 3d/enclosure/verify.py:
      // front_shell (+68mm) > screen & mics-keys (+36mm) > mid_frame (+14mm) > MainBoard (-4mm)
      // > rear_retainer (-18mm) > camera_clamp (-22mm) > 104050 battery & 3525 speaker (-34mm)
      // > GC0308 camera (-38mm) > rear_shell (-58mm)
      const t = currentExplode
      if (partObjects.front_shell) partObjects.front_shell.position.z = 0.68 * t
      if (partObjects.hw_screen) partObjects.hw_screen.position.z = 0.36 * t
      if (partObjects.hw_mics_keys) partObjects.hw_mics_keys.position.z = 0.36 * t
      if (partObjects.mid_frame) partObjects.mid_frame.position.z = 0.14 * t
      if (partObjects.hw_mainboard) partObjects.hw_mainboard.position.z = -0.04 * t
      if (partObjects.rear_retainer) partObjects.rear_retainer.position.z = -0.18 * t
      if (partObjects.camera_clamp) partObjects.camera_clamp.position.z = -0.22 * t
      if (partObjects.hw_rear_modules) partObjects.hw_rear_modules.position.z = -0.34 * t
      if (partObjects.hw_camera) partObjects.hw_camera.position.z = -0.38 * t
      if (partObjects.rear_shell) partObjects.rear_shell.position.z = -0.58 * t
      if (partObjects.desktop_cradle) partObjects.desktop_cradle.position.y = -0.32 * t

      for (const sw of switchMeshes) {
        if (sw.press > 0.005) {
          sw.press *= 0.82
        } else {
          sw.press = 0
        }
        sw.mesh.position.z = sw.baseZ - sw.press * 0.016
      }

      if (cameraLerp?.active && camera && controls) {
        camera.position.lerp(cameraLerp.pos, 0.1)
        controls.target.lerp(cameraLerp.target, 0.1)
        if (
          camera.position.distanceTo(cameraLerp.pos) < 0.01 &&
          controls.target.distanceTo(cameraLerp.target) < 0.01
        ) {
          cameraLerp.active = false
        }
      }

      controls?.update()
      if (renderer && scene && camera) {
        renderer.render(scene, camera)
      }
    }
    animate()
  } catch (err) {
    console.warn('RodakOS 3D enclosure viewer fallback:', err)
    loadState.value = 'fallback'
  }
})

onBeforeUnmount(() => {
  if (import.meta.client) {
    document.body.style.overflow = ''
    window.removeEventListener('keydown', handleFullscreenKeydown)
  }
  if (io10ClickTimer) clearTimeout(io10ClickTimer)
  cleanupPointerListeners?.()
  if (rafId) window.cancelAnimationFrame(rafId)
  resizeObserver?.disconnect()
  controls?.dispose()
  textureCache.forEach((tex) => tex.dispose())
  textureCache.clear()
  if (scene) {
    scene.traverse((obj) => {
      const mesh = obj as THREE.Mesh
      if (mesh.isMesh) {
        mesh.geometry?.dispose()
        if (Array.isArray(mesh.material)) {
          mesh.material.forEach((m) => m.dispose())
        } else {
          mesh.material?.dispose()
        }
      }
    })
  }
  renderer?.dispose()
  renderer?.domElement.remove()
  renderer = null
  scene = null
  camera = null
  controls = null
})
</script>

<template>
  <div class="relative">
    <!-- 全屏时在原文档流保留占位高度，避免页面滚动条位置跳动 -->
    <div
      v-if="isFullscreen"
      class="flex h-[468px] items-center justify-center rounded-xl border border-dashed border-default bg-elevated/30 text-xs text-muted"
    >
      {{ isEn ? '3D CAD Viewer open in fullscreen' : '3D CAD 模型已在全屏模式打开' }}
    </div>

    <!-- 全屏时 Teleport 到 body，彻底脱离 UPageSection 的 isolate 层叠上下文 -->
    <Teleport to="body" :disabled="!isFullscreen">
      <!-- 3D 模型全屏背景遮罩 -->
      <div
        v-if="isFullscreen"
        class="fixed inset-0 z-[100] bg-slate-950/85 backdrop-blur-md"
        @click="isFullscreen = false"
      />

      <div
        class="flex flex-col overflow-hidden rounded-xl border border-default ring-1 ring-default"
        :class="
          isFullscreen
            ? 'fixed inset-3 z-[101] bg-default shadow-2xl sm:inset-5'
            : 'bg-elevated/40'
        "
      >
        <!-- 顶部视图预设与外壳配色条 -->
        <div
          class="flex shrink-0 flex-wrap items-center justify-between gap-2 border-b border-default bg-elevated/60 px-3.5 py-2.5"
        >
          <div class="flex flex-wrap items-center gap-1">
            <UButton
              v-for="preset in viewPresets"
              :key="preset.id"
              :label="preset.label"
              :icon="preset.icon"
              :color="activeView === preset.id ? 'primary' : 'neutral'"
              :variant="activeView === preset.id ? 'soft' : 'ghost'"
              size="xs"
              @click="selectPreset(preset.id)"
            />
          </div>

          <div class="flex items-center gap-2">
            <div class="flex items-center gap-1.5">
              <button
                v-for="f in filaments"
                :key="f.id"
                type="button"
                class="flex size-5 cursor-pointer items-center justify-center rounded-full transition-transform"
                :class="
                  activeFilament === f.id
                    ? 'scale-110 ring-2 ring-primary ring-offset-2 ring-offset-default'
                    : 'opacity-80 hover:opacity-100'
                "
                :title="f.label"
                :aria-label="f.label"
                @click="applyFilamentColors(f.id)"
              >
                <span class="size-3.5 rounded-full" :class="f.dotClass" />
              </button>
            </div>
          </div>
        </div>

        <!-- 实时 3D 画布舞台（支持全屏展开 3D CAD 模型本身） -->
        <div
          class="relative w-full overflow-hidden bg-[radial-gradient(ellipse_at_center,color-mix(in_srgb,var(--ui-primary)_10%,transparent)_0%,transparent_72%)]"
          :class="isFullscreen ? 'min-h-0 flex-1' : 'h-[340px] sm:h-[380px]'"
        >
          <div
            ref="canvasHost"
            class="absolute inset-0"
            :class="isHoverClickable ? 'cursor-pointer' : 'cursor-grab active:cursor-grabbing'"
          />

          <div
            v-if="loadState === 'loading'"
            class="pointer-events-none absolute inset-0 flex flex-col items-center justify-center gap-2 text-xs text-muted"
          >
            <UIcon name="i-lucide-loader-2" class="size-5 animate-spin text-primary" />
            <span>{{ isEn ? 'Loading BigSmart-Open enclosure CAD mesh...' : '正在加载 BigSmart-Open 参数化外壳 CAD 模型...' }}</span>
          </div>

          <div v-if="loadState === 'fallback'" class="absolute inset-0 bg-white p-2">
            <img
              :src="withBase('hardware/enclosure-exploded.webp')"
              alt="RodakOS 3d/enclosure assembly"
              class="h-full w-full object-contain"
            >
          </div>

          <!-- 右上角 3D 模型全屏查看与桌面重置 -->
          <div
            v-if="loadState === 'ready'"
            class="absolute top-2.5 right-2.5 flex items-center gap-1.5 rounded-full border border-default bg-default/85 p-1 shadow-md backdrop-blur-md"
          >
            <UButton
              v-if="screenScene !== 'home'"
              :label="isEn ? 'Home' : '返回桌面'"
              icon="i-lucide-home"
              color="neutral"
              variant="ghost"
              size="xs"
              @click="screenScene = 'home'"
            />
            <UButton
              :label="
                isFullscreen
                  ? isEn
                    ? 'Exit Fullscreen'
                    : '退出全屏'
                  : isEn
                    ? 'Fullscreen 3D'
                    : '全屏查看 3D'
              "
              :icon="isFullscreen ? 'i-lucide-minimize-2' : 'i-lucide-maximize-2'"
              color="primary"
              variant="soft"
              size="xs"
              @click="toggleFullscreen"
            />
          </div>

          <!-- 悬浮热点交互提示 -->
          <div
            v-if="hoverHint && loadState === 'ready'"
            class="pointer-events-none absolute top-2.5 left-3 inline-flex items-center gap-1.5 rounded-full border border-primary/40 bg-default/90 px-2.5 py-1 text-[11px] font-medium text-highlighted shadow-md backdrop-blur-sm"
          >
            <UIcon name="i-lucide-mouse-pointer-click" class="size-3.5 text-primary" />
            <span>{{ hoverHint }}</span>
          </div>

          <!-- 左下角操作提示 -->
          <div
            v-if="loadState === 'ready'"
            class="pointer-events-none absolute bottom-2.5 left-3 hidden items-center gap-1.5 rounded-full border border-default bg-default/80 px-2.5 py-0.5 text-[11px] text-muted backdrop-blur-sm sm:inline-flex"
          >
            <UIcon name="i-lucide-move-3d" class="size-3 text-primary" />
            <span>
              {{
                isFullscreen
                  ? isEn
                    ? 'Fullscreen 3D CAD · Drag to orbit · Scroll to zoom · Press Esc to exit'
                    : '3D CAD 全屏检视 · 拖拽旋转 · 滚轮缩放 · 点击屏幕/按键交互 · 按 Esc 退出'
                  : isEn
                    ? 'Drag to orbit · Tap 3D LCD icons or physical keys · Slide Explode for true Z-stack'
                    : '拖拽旋转 · 直接点击 3D 屏幕或实体键 · 拖动下方滑块查看真实九层装配栈'
              }}
            </span>
          </div>
        </div>

        <!-- 底部爆炸分解滑块 -->
        <div
          class="flex shrink-0 flex-wrap items-center justify-between gap-3 border-t border-default bg-elevated/60 px-4 py-2.5"
        >
          <label class="flex flex-1 items-center gap-3 text-xs text-muted">
            <span class="inline-flex shrink-0 items-center gap-1.5 font-medium text-highlighted">
              <UIcon name="i-lucide-layers" class="size-3.5 text-primary" />
              {{ isEn ? 'Explode' : '爆炸分解' }}
            </span>
            <input
              v-model.number="explode"
              type="range"
              min="0"
              max="100"
              step="1"
              aria-label="爆炸分解 / Explode enclosure"
              class="h-1.5 w-full max-w-xs cursor-pointer accent-sky-500"
            >
            <span class="w-9 shrink-0 text-right font-mono text-[11px] tabular-nums text-primary">
              {{ explode }}%
            </span>
          </label>
          <span class="font-mono text-[11px] text-dimmed">6 CAD Parts · 106 × 86 × 33.2 mm</span>
        </div>
      </div>
    </Teleport>
  </div>
</template>
