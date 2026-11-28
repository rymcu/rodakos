import { SITE_LINKS } from '~/config/links'
import type { LandingCopy } from '~/types/landing'

export const LANDING_COPY_EN: LandingCopy = {
  meta: {
    title: 'RodakOS — Open-Source Local-First Pocket "Phone OS" for ESP32-S3',
    description:
      'Open-source ESP32-S3 firmware built on ESP-IDF 6.0.2 and LVGL 9.3. Packs 16 native touch apps, local MultiNet wake with frame-synced voice captions, USB-free SRP6a LAN onboarding, and immutable Recovery signed OTA on a 320×240 screen.'
  },
  nav: {
    links: [
      { label: 'Features', to: '/#highlights', hash: '#highlights' },
      { label: 'Shell & Apps', to: '/#apps-shell', hash: '#apps-shell' },
      { label: 'Voice & Protocols', to: '/#voice', hash: '#voice' },
      { label: 'Hardware', to: '/#hardware', hash: '#hardware' },
      { label: 'Build & Flash', to: '/#build-flash', hash: '#build-flash' }
    ],
    docsLabel: 'Docs',
    protocolsLabel: 'Protocol Lab',
    githubCta: 'GitHub',
    searchLabel: 'Search docs',
    localeToggleLabel: '中文'
  },
  hero: {
    badge: 'Open-Source ESP32-S3 Pocket Phone OS · ESP-IDF 6.0.2 · LVGL 9.3 · v0.1.6',
    titleLine1: 'Turn a 320×240 ESP32-S3 Display into',
    titleLine2: 'a Calm, Verifiable Pocket "Phone OS".',
    description:
      'RodakOS is an open-source embedded firmware experiment for RYMCU BigSmart. Inside 16 MB flash and 8 MB PSRAM, it brings together a paged touch Home, 16 native apps, local wake word with live reply captions, pinned LAN trust onboarding, and immutable Recovery OTA.',
    primaryCta: 'Explore on GitHub',
    secondaryCta: 'Try Protocol Lab',
    docsCta: 'Read Architecture Docs',
    hint: 'Open firmware at rymcu/rodakos · 64 host ASan/UBSan test suites · Companion to the Rodak local workbench',
    previewTabs: [
      {
        id: 'home',
        label: 'Paged Home',
        icon: 'i-lucide-layout-grid',
        title: 'Home Launcher · Paged Grid, Folders, Arrange Mode & 4 Built-in Themes',
        alt: 'Real LVGL 9.3 320×240 render of the RodakOS Home screen showing category-gradient Ionicons tiles, top status bar, and page dots.',
        caption:
          'Supports up to 8 pages plus a read-only All Apps view while keeping widgets resident only for the active page and its two neighbors. Toggle the 4 native firmware themes live below.',
        darkSrc: 'screenshots/home-dark.webp',
        lightSrc: 'screenshots/home-light.webp',
        supportsFirmwareThemes: true
      },
      {
        id: 'voice-caption',
        label: 'Voice Captions',
        icon: 'i-lucide-captions',
        title: 'Shell Voice Bar · Turn Progress & Frame-Synced Reply Captions',
        alt: 'Real LVGL 9.3 320×240 render of the RodakOS Home screen with the Shell-owned smoked-glass bottom voice caption bar on lv_layer_top().',
        caption:
          'Rendered by PhoneShell on lv_layer_top() over any running app, displaying turn.progress steps and output.caption lines synced to the written RAV1 frame — yielding automatically to keyboards and display shares.',
        darkSrc: 'screenshots/voice-caption-dark.webp',
        lightSrc: 'screenshots/voice-caption-light.webp'
      },
      {
        id: 'calendar',
        label: 'Lunar Calendar',
        icon: 'i-lucide-calendar-days',
        title: 'Calendar · Offline 1900–2100 Lunar Dates, Solar Terms & Festivals',
        alt: 'Real LVGL 9.3 320×240 render of the RodakOS month calendar showing Gregorian dates paired with 12 px lunar dates in every cell.',
        caption:
          'Fits a full 6-week month grid on 320×240 using an offline lunar_calendar engine and dedicated 12 px lunar font subset, with swipe navigation, day view, and in-app SNTP time sync.',
        darkSrc: 'screenshots/calendar-dark.webp',
        lightSrc: 'screenshots/calendar-light.webp'
      },
      {
        id: 'gyro',
        label: 'Bubble Level',
        icon: 'i-lucide-compass',
        title: 'Gyro · 154×154 RGB565 Bubble Level & Six-Axis Centered Bars',
        alt: 'Real LVGL 9.3 320×240 render of the RodakOS Gyro app with a circular bubble canvas on the left and roll, pitch, yaw readouts plus zero and freeze controls on the right.',
        caption:
          'Samples QMI8658 on a background task so I2C never runs inside the LVGL thread. Supports relative zeroing, freeze-frame inspection, and a six-axis live bar view.',
        darkSrc: 'screenshots/gyro-dark.webp',
        lightSrc: 'screenshots/gyro-light.webp'
      },
      {
        id: 'smart',
        label: 'Smart Lights',
        icon: 'i-lucide-lightbulb',
        title: 'Smart · Atomic Single-Field Light Patches & Voice Light Overlay',
        alt: 'Real LVGL 9.3 320×240 render of the RodakOS Smart lighting overview showing multiple room lights, brightness, and toggles.',
        caption:
          'Writes single-field ApplyLightPatch updates by light ID, commits brightness on release, saves color picks without unexpectedly powering a light on, and reflects MQTT changes every 500 ms.',
        darkSrc: 'screenshots/smart-dark.webp',
        lightSrc: 'screenshots/smart-light.webp'
      },
      {
        id: 'themes',
        label: 'Themes & Trust',
        icon: 'i-lucide-palette',
        title: 'Themes · Color Presets, Signed SD Appearance Packs & Touch Trust',
        alt: 'Real LVGL 9.3 320×240 render of the RodakOS Themes app displaying built-in presets, appearance pack status, and publisher trust controls.',
        caption:
          'Boot logos, wallpapers, and custom color themes deploy as signed SD packages independent of firmware OTA, requiring physical touch confirmation on screen to trust a publisher.',
        darkSrc: 'screenshots/themes-dark.webp',
        lightSrc: 'screenshots/themes-light.webp'
      }
    ],
    showcase: {
      realRenderBadge: 'Live Interactive Vector OS',
      demoDataNote:
        'Both the front-shell prototype and 3D CAD enclosure follow real LVGL 9.3 layouts and device interactions: top pull-down for Control Center, horizontal swipes on Home/Calendar, POWER sleep, IO0 BOOT Home, and IO10 single/double/long press for Control Center / Smart / Lock.',
      themeLabel: 'Switch firmware preset:',
      themeNames: {
        dark: 'Dark',
        light: 'Light',
        blue: 'Blue',
        green: 'Green'
      },
      expandLabel: 'Interactive Fullscreen',
      zoomInLabel: 'Zoom 1:1',
      fitLabel: 'Fit Window',
      closeLabel: 'Close',
      mobileHint: 'Tap icons directly or open fullscreen interactive mode',
      prevSlideLabel: 'Previous screen',
      nextSlideLabel: 'Next screen',
      carouselHint:
        'Tap any app icon, switch, or control directly in crisp fullscreen vector mode.'
    }
  },
  highlights: {
    headline: 'Why RodakOS',
    title: 'Real embedded discipline inside 16 MB flash and 8 MB PSRAM',
    description:
      'Built like a real operating system rather than a demo sketch: explicit HAL boundaries, predictable SRAM headroom, pinned LAN trust, and recovery-guarded updates.',
    items: [
      {
        id: 'shell',
        icon: 'i-lucide-layers',
        badge: 'Phone OS Shell',
        title: 'Single Lifecycle Host & Non-Destructive Global Overlays',
        description:
          'A frozen static registry validates unique IDs and aliases; PhoneAppHost owns OnCreate/Resume/Pause/Destroy while PhoneShell renders Lock Screen, Control Center, and the Voice Bar on lv_layer_top().'
      },
      {
        id: 'on-demand',
        icon: 'i-lucide-cpu',
        badge: 'On-Demand HAL',
        title: 'Heavy Hardware Opens on Demand and Returns Internal DMA',
        description:
          'Audio codecs, GC0308 camera, voice, and SD storage stay closed at boot. Resident stacks and the LVGL heap live in PSRAM, returning the largest internal DMA block to the 8 KiB gate after Camera exits.'
      },
      {
        id: 'wake',
        icon: 'i-lucide-mic',
        badge: 'Local Wake',
        title: 'Zero Idle Cloud Socket — Full-Duplex Barge-In on Wake',
        description:
          'Embedded ESP-SR MultiNet listens locally with wake-time AEC and AGC. No WebSocket opens while idle; once woken, a dedicated playback task keeps speaker writes from stalling uplink barge-in.'
      },
      {
        id: 'trust',
        icon: 'i-lucide-shield-check',
        badge: 'Pinned LAN Trust',
        title: 'USB-Free 6-Digit SRP6a First Setup & Shared Server Trust',
        description:
          'Establish server trust over USB or over the LAN using a 6-digit on-screen SRP6a code. HTTPS, MQTTS, and WSS share one pinned .local identity; DNS-SD candidates are verified via TLS before promotion.'
      },
      {
        id: 'ota',
        icon: 'i-lucide-refresh-cw',
        badge: 'Immutable Recovery',
        title: '2.5 MiB Immutable Recovery + 13.3 MiB Signed SD-Staged OTA',
        description:
          'Staging OTA payloads on SD keeps one 13.3125 MiB ota_0 slot for firmware and speech models. Only the immutable factory Recovery may rewrite ota_0, guarded by an isolated journal and per-task RSA-2048 signatures.'
      },
      {
        id: 'verified',
        icon: 'i-lucide-flask-conical',
        badge: '64 Host Suites',
        title: 'Real LVGL 9.3 Host Regression & Hash-Checked Overlays',
        description:
          '64 host test suites compile real UI, audio, MQTT, voice, and OTA code under Linux/WSL with ASan/UBSan leak checks. CMake verifies upstream hashes before applying managed-component patches.'
      }
    ]
  },
  appsShell: {
    headline: 'Shell & 16 Built-In Apps',
    title: 'A standalone pocket utility offline — a full voice companion with Rodak',
    description:
      'All 16 built-in apps register statically through PhoneAppRegistry and share theme tokens, layout helpers, and path-scoped storage leases. Most utilities work completely offline.',
    offlineBadge: 'Offline Ready',
    cloudBadge: 'With Rodak',
    features: [
      {
        icon: 'i-lucide-panels-top-left',
        title: '3-Page Resident Render Window (Up to 8 Pages + All Apps)',
        description:
          'Home keeps lightweight tile shells for every page but materializes icon grids only for the active page and its two immediate neighbors, releasing far pages asynchronously after scroll.'
      },
      {
        icon: 'i-lucide-hand',
        title: 'Cached GT911 Polling Bridge — Never Call I2C in LVGL',
        description:
          'Low-priority background tasks poll GT911 touch and QMI8658 motion into cached state so LVGL callbacks never touch I2C, preventing touch hangs on interrupt-less wiring.'
      },
      {
        icon: 'i-lucide-sliders-horizontal',
        title: 'Customisable IO10 Side Key Actions',
        description:
          'Single click toggles Control Center, double click opens Smart, and long press locks the device — all overridable via NVS-backed bindings cached cleanly in RAM.'
      }
    ],
    categories: [
      {
        id: 'system',
        label: 'System & Shell',
        icon: 'i-lucide-layout-grid',
        sceneId: 'home-page2',
        screenTitle: 'Home Page 2 · Wake, Themes & Messages Launcher & Appearance Controls',
        screenCaption:
          'Home persists versioned exact-ID layouts and folders with one guarded save; Settings and Themes manage Wi-Fi, brightness, early-boot USB MSC disk mode, and signed appearance packs.',
        screenAlt: 'Real LVGL 9.3 320×240 render of RodakOS Home page 2 showing Wake, Themes, and Messages tiles.',
        darkSrc: 'screenshots/home-page2-dark.webp',
        note: 'Home, Settings, Themes, and System form the standalone core alongside Shell-owned Lock Screen and Control Center overlays.',
        apps: [
          {
            id: 'home',
            name: 'Home Launcher',
            icon: 'i-lucide-home',
            slotColor: '#5A6E8C',
            summary: 'Paged icon grid, folders, Arrange mode, unread dot, and 3-page residency window',
            offline: true
          },
          {
            id: 'settings',
            name: 'Settings',
            icon: 'i-lucide-settings',
            slotColor: '#5A6E8C',
            summary: 'Wi-Fi credentials, backlight, one-shot early-boot USB MSC disk mode & web files',
            offline: true
          },
          {
            id: 'themes',
            name: 'Themes',
            icon: 'i-lucide-palette',
            slotColor: '#5A6E8C',
            summary: '4 built-in palettes, signed SD appearance pack status & physical touch trust',
            offline: true
          },
          {
            id: 'system',
            name: 'System Info',
            icon: 'i-lucide-info',
            slotColor: '#5A6E8C',
            summary: 'Firmware version, Wi-Fi state, internal SRAM / PSRAM headroom & SD capacity',
            offline: true
          }
        ]
      },
      {
        id: 'voice-smart',
        label: 'Voice & Smart',
        icon: 'i-lucide-sparkles',
        sceneId: 'assistant',
        screenTitle: 'Assistant · Wake State, Session Phase & Cloud Diagnostics',
        screenCaption:
          'Voice runs as a system service independent of app teardown. Paired with Smart lighting, Messages todo inbox, and Wake-on-LAN.',
        screenAlt: 'Real LVGL 9.3 320×240 render of the RodakOS Assistant app showing wake toggle, voice status light, and cloud diagnostics.',
        darkSrc: 'screenshots/assistant-dark.webp',
        note: 'Wake-on-LAN only needs local Wi-Fi; voice turns, inbox reminders, and home device sync pair with Rodak over pinned TLS.',
        apps: [
          {
            id: 'assistant',
            name: 'Assistant',
            icon: 'i-lucide-message-circle',
            slotColor: '#23919F',
            summary: 'Wake monitoring toggle, live turn status, voice status LED & cloud fix guide',
            offline: false
          },
          {
            id: 'smart',
            name: 'Smart Lights',
            icon: 'i-lucide-lightbulb',
            slotColor: '#23919F',
            summary: 'Single-light detail or multi-light overview with atomic MQTT patches',
            offline: false
          },
          {
            id: 'messages',
            name: 'Messages Inbox',
            icon: 'i-lucide-bell',
            slotColor: '#C23A6E',
            summary: 'Notices and todo reminders with complete & 10/30/60m snooze; hidden during screen share',
            offline: false
          },
          {
            id: 'wol',
            name: 'Wake (WoL)',
            icon: 'i-lucide-power',
            slotColor: '#C23A6E',
            summary: 'Persist up to 8 LAN hosts and send validated UDP Wake-on-LAN magic packets',
            offline: true
          }
        ]
      },
      {
        id: 'media',
        label: 'Media',
        icon: 'i-lucide-disc-3',
        sceneId: 'recorder',
        screenTitle: 'Recorder · 20-Bar 48 dB Input Level Meter & Atomic WAV Finalization',
        screenCaption:
          'Recorder, Music, Photos, and Camera claim codecs, DVP, and path-scoped storage leases on demand. Recorder reports Saved only after WAV header rewrite, flush, and close succeed.',
        screenAlt: 'Real LVGL 9.3 320×240 render of the RodakOS Recorder app with timer, 20-bar live level meter, and shutter-style record button.',
        darkSrc: 'screenshots/recorder-dark.webp',
        note: 'Media apps separate empty libraries from storage/read failures with explicit Retry actions and SPIRAM buffering.',
        apps: [
          {
            id: 'recorder',
            name: 'Recorder',
            icon: 'i-lucide-mic',
            slotColor: '#D0644E',
            summary: '20-bar 48 dB live meter, speaker test, instant replay & verified WAV header write',
            offline: true
          },
          {
            id: 'music',
            name: 'Music',
            icon: 'i-lucide-music',
            slotColor: '#D0644E',
            summary: 'Scan /music and play 16-bit PCM WAV & MP3 asynchronously off the UI thread',
            offline: true
          },
          {
            id: 'camera',
            name: 'Camera',
            icon: 'i-lucide-camera',
            slotColor: '#D0644E',
            summary: 'On-demand GC0308 DVP preview/capture with WebRTC peer mutual exclusion',
            offline: true
          },
          {
            id: 'photos',
            name: 'Photos',
            icon: 'i-lucide-image',
            slotColor: '#B87A22',
            summary: 'Browse JPG/PNG/BMP under /photos and /DCIM using SPIRAM-backed image loading',
            offline: true
          }
        ]
      },
      {
        id: 'tools',
        label: 'Utilities',
        icon: 'i-lucide-wrench',
        sceneId: 'wol',
        screenTitle: 'Utilities · Lunar Calendar, Bubble Level, Clock, Files & Wake-on-LAN',
        screenCaption:
          'Tuned for crisp 320×240 touch targets and clear error states. Files uses confirmed deletes and refuses to remove protected paths under /rodakos.',
        screenAlt: 'Real LVGL 9.3 320×240 render of the RodakOS Wake-on-LAN host list with edit cards and dedicated power send buttons.',
        darkSrc: 'screenshots/wol-dark.webp',
        lightSrc: 'screenshots/wol-light.webp',
        note: 'All utilities run offline; Clock and Calendar offer one-tap in-app SNTP sync once Wi-Fi is connected.',
        apps: [
          {
            id: 'calendar',
            name: 'Calendar',
            icon: 'i-lucide-calendar',
            slotColor: '#3D7DD3',
            summary: 'Offline 1900–2100 lunar dates, solar terms & festivals with day view and SNTP sync',
            offline: true
          },
          {
            id: 'gyro',
            name: 'Gyro Level',
            icon: 'i-lucide-compass',
            slotColor: '#3D7DD3',
            summary: '154×154 RGB565 bubble level (zero / freeze) plus centered six-axis sensor bars',
            offline: true
          },
          {
            id: 'clock',
            name: 'Clock',
            icon: 'i-lucide-clock',
            slotColor: '#B87A22',
            summary: 'Clean local clock display with network SNTP time synchronization',
            offline: true
          },
          {
            id: 'files',
            name: 'Files',
            icon: 'i-lucide-folder',
            slotColor: '#7A7068',
            summary: 'Browse SD folders, view images full-screen, check free space & confirmed deletes',
            offline: true
          }
        ]
      }
    ]
  },
  voice: {
    headline: 'Device Voice Pipeline & Open Protocols',
    title: 'From local wake word to frame-synced captions — governed by open contracts',
    description:
      'RodakOS speaks open, machine-readable contracts (rodak-aiot/v1 and rodak-realtime-voice/v1) shipped with OpenAPI, AsyncAPI, JSON Schemas, 174 fixtures, and an interactive browser protocol lab.',
    diagramBadge: '5-Stage Firmware Pipeline',
    diagramNote:
      'While idle, only local MultiNet wake runs on ESP32-S3 with no voice WebSocket open. Full conversational turns and announcements require a paired Rodak server.',
    labCta: 'Open Protocol Lab',
    stages: [
      {
        id: 'wake',
        step: '01',
        title: 'Local Wake',
        engine: 'ESP-SR MultiNet + AEC/AGC',
        metric: 'Zero Idle Socket',
        icon: 'i-lucide-ear',
        summary: 'Runs local wake-word detection on device with synchronous wake-time AEC and AGC, playing a short chime on match.',
        highlights: [
          'Zero network voice sockets open while listening locally',
          'Detection window automatically restarts across silent intervals',
          'Reserves ES7210 ADC headroom during loud playback to keep barge-in clean'
        ]
      },
      {
        id: 'connect',
        step: '02',
        title: 'Pinned Session',
        engine: 'Pinned WSS + HS256 JWT',
        metric: '600s Auto-Refresh',
        icon: 'i-lucide-lock',
        summary: 'Claims exclusive audio focus and opens a pinned rodak-realtime-voice/v1 WebSocket, silently refreshing 600s tokens before expiry.',
        highlights: [
          'Shares the exact server trust root used by HTTPS and MQTTS',
          'Cleanly stops any active remote Camera WebRTC stream before opening WSS',
          'Rejected refresh aborts the attempt without wiping pairing credentials'
        ]
      },
      {
        id: 'duplex',
        step: '03',
        title: 'Duplex & Barge-In',
        engine: 'RAV1 + Dual Tasks',
        metric: 'Core 1 Playback',
        icon: 'i-lucide-audio-waveform',
        summary: 'assistant_io decodes RAV1 frames into a 50-frame queue (~3s) while a separate assistant_play task (6 KiB PSRAM stack) feeds ES8311.',
        highlights: [
          'Speaker writes never block uplink capture, keeping barge-in responsive',
          'Fixed 4-slot 16-bit I2S TX frame avoids DMA ring fragmentation in internal RAM',
          'Four message gates (generation, sessionId, playbackEpoch, sequence) reject stale frames'
        ]
      },
      {
        id: 'caption',
        step: '04',
        title: 'Synced Captions',
        engine: 'turn.progress + output.caption',
        metric: 'Shell Overlay',
        icon: 'i-lucide-captions',
        summary: 'Shows thinking/tool steps via turn.progress and advances output.caption lines strictly at the written RAV1 frame boundary.',
        highlights: [
          'Frosted glass bar sits on lv_layer_top() over any active app',
          'Tap to dismiss; automatically yields to soft keyboard, modals, and display share',
          'Follow-up turns stay on one session until "goodbye" or 30s silence re-arms local wake'
        ]
      },
      {
        id: 'announce',
        step: '05',
        title: 'Announce & Receipts',
        engine: 'MQTT voice.announce',
        metric: 'Zero MCP Surface',
        icon: 'i-lucide-megaphone',
        summary: 'Server-initiated voice.announce opens playback-only sessions. Firmware exposes zero MCP tools; volume and light run as correlated MQTT effects.',
        highlights: [
          'MQTT callback only queues the announce request and ACKs promptly',
          'Volume and light effects bind to connection epochs without blind replay',
          'Clearly distinguishes software receipt confirmation from physical verification'
        ]
      }
    ],
    cards: [
      {
        icon: 'i-lucide-network',
        title: 'Three Channels, One Token',
        description:
          'HTTP handles identity, pairing, token refresh, and OTA downloads; MQTT carries telemetry, shadow, and commands; WebSocket opens only for voice — all using one HS256 JWT.'
      },
      {
        icon: 'i-lucide-history',
        title: '64-Entry Replay Ledger',
        description:
          'Commands keep a bounded 64-entry authority cache with connection-epoch isolation so stale callbacks and reconnect races never double-apply state.'
      },
      {
        icon: 'i-lucide-file-code-2',
        title: 'Machine-Readable Contracts & 174 Fixtures',
        description:
          'OpenAPI, AsyncAPI, and JSON Schemas live under contracts/ with valid and invalid fixtures you can inspect and test live in the browser Protocol Lab.'
      }
    ]
  },
  hardware: {
    headline: 'Hardware, 3D Enclosure & Ecosystem',
    title: 'Clean HAL abstraction from open dev board to printable desktop handset',
    description:
      'Board definitions stay inside rodakos_hal_boards + Espressif Board Manager. Pair BigSmart-Open and its parametric 3D enclosure with the Rodak workbench for a complete local AIoT loop.',
    projects: [
      {
        id: 'hardware',
        badge: 'Open Hardware',
        repoLabel: 'rymcu/BigSmart-Open',
        title: 'BigSmart-Open Reference Board',
        description:
          'The supported reference board for RodakOS. Powered by ESP32-S3-WROOM-1-N16R8 with a 320×240 ST7789 capacitive touchscreen, ES8311/ES7210 audio codecs, GC0308 DVP camera, QMI8658 IMU, PCA9557 expander, and WS2812 RGB.',
        icon: 'i-lucide-circuit-board',
        to: SITE_LINKS.hardware,
        specs: [
          'ESP32-S3 · 16MB Flash / 8MB PSRAM',
          '320×240 ST7789 + GT911 Touch',
          'ES8311 + ES7210 Dual Mic & Speaker',
          'GC0308 DVP + QMI8658 + SDMMC'
        ],
        ctaLabel: 'Visit BigSmart-Open Repo'
      },
      {
        id: 'rodak',
        badge: 'Companion Workbench',
        repoLabel: 'rymcu/rodak-releases',
        title: 'Rodak Desktop & NAS Server',
        description:
          'The local-first desktop and NAS hub paired with RodakOS: visual USB flashing, one-click trusted provisioning, live 320×240 Appearance Studio compiling, local SenseVoice/Kokoro speech, and WebRTC screen mirroring.',
        icon: 'i-lucide-monitor',
        to: SITE_LINKS.rodakReleases,
        specs: [
          'Windows Desktop + NAS Docker',
          'Visual USB Flashing & Trust Setup',
          '320×240 Appearance Studio',
          'WebRTC Screen Mirror & Touch'
        ],
        ctaLabel: 'Open Rodak Releases'
      }
    ],
    enclosureHeadline: 'Parametric Enclosure & Flash Layout',
    enclosureTitle: 'BigSmart-Open build123d 3D enclosure & SD-staged 16 MB Recovery layout',
    enclosureDescription:
      'The companion BigSmart-Open hardware repository hosts the zero-interference parametric 6-part FDM enclosure with a 15° desktop cradle, while RodakOS uses SD-staged OTA to give the main slot 13.3125 MiB alongside an immutable 2.5 MiB Recovery partition.',
    enclosureCta: 'Read Hardware & Enclosure Guide',
    enclosureFeatures: [
      {
        icon: 'i-lucide-box',
        title: '6 Support-Free Printable Parts + 15° Desktop Cradle',
        description:
          '106.0 × 86.0 × 33.2 mm body verified via full B-rep interference checks, featuring dual-mic acoustic ports, sealed speaker baffle, and a 0.60 mm diffused RGB window.'
      },
      {
        icon: 'i-lucide-wrench',
        title: 'Only Two Fastener Sizes Across the Entire Build (M2 & M3)',
        description:
          '19 standardized screws in total; adjust lengths in params.py to recalculate wall clearances and regenerate STL/STEP files with one uv command.'
      },
      {
        icon: 'i-lucide-hard-drive',
        title: 'Board Manager HAL Ready for Future Ports',
        description:
          'Hardware tables live strictly under components/rodakos_hal_boards/; new boards follow boards/<vendor>/<board>/ with explicit build selection and hardware acceptance.'
      }
    ],
    partitionTitle: '16 MB Flash Partition Table (partitions_16m.csv)',
    partitionBadge: '16 MiB · SD-Staged OTA',
    partitionSummary:
      'Dual A/B app slots on 16 MB flash cap each firmware image around 7 MB. Staging signed OTA images on SD reserves 13.3125 MiB for the main app and speech models while keeping a 2.5 MiB immutable Recovery safety net.',
    partitions: [
      {
        name: 'sys',
        subType: 'nvs / otadata / ota_state',
        offset: '0x9000',
        sizeLabel: '60 KiB',
        widthPercent: 8,
        colorClass: 'bg-slate-600',
        role: 'NVS settings, OTA boot selector, PHY init data, and isolated A/B OTA state journal.'
      },
      {
        name: 'recovery',
        subType: 'app / factory',
        offset: '0x20000',
        sizeLabel: '2.5 MiB',
        widthPercent: 20,
        colorClass: 'bg-amber-600',
        role: 'Independently built immutable factory Recovery — the only runtime allowed to rewrite ota_0.'
      },
      {
        name: 'app (ota_0)',
        subType: 'app / ota_0',
        offset: '0x2a0000',
        sizeLabel: '13.3125 MiB',
        widthPercent: 66,
        colorClass: 'bg-sky-600',
        role: 'Main RodakOS slot hosting all 16 apps, LVGL 9.3, CJK/icon subsets, and embedded MultiNet wake models.'
      },
      {
        name: 'core',
        subType: 'data / coredump',
        offset: '0xff0000',
        sizeLabel: '64 KiB',
        widthPercent: 6,
        colorClass: 'bg-rose-600',
        role: 'Crash coredump storage paired with LVGL trap-on-assert diagnostics.'
      }
    ],
    enclosureImageCaption:
      'Parametric exploded assembly and 15° desktop cradle render from BigSmart-Open/enclosure (build123d B-rep verified).',
    enclosureImageAlt:
      'Exploded assembly and 15-degree desktop cradle render of the parametric build123d RodakOS enclosure.'
  },
  buildFlash: {
    headline: 'Build, Flash & Verify',
    title: 'Flash visually with Rodak, or build cleanly from ESP-IDF 6.0.2 source',
    description:
      'Project scripts pin ESP-IDF 6.0.2 and its recommended Xtensa GCC without requiring a global PowerShell profile, and verify immutable Recovery hashes before writing flash.',
    quickstart: {
      badge: 'Fastest Path · No ESP-IDF Setup Required',
      title: 'Visual Flashing & Provisioning via Rodak',
      description:
        'Connect BigSmart over USB-C to flash a merged Recovery image and inject workspace trust in one flow, or use the on-screen 6-digit SRP6a code over LAN without USB.',
      icon: 'i-lucide-usb',
      highlights: [
        'First Recovery migration flashes the merged image; routine OTA uploads only build/rodakos.bin',
        'Supports both wired USB server-trust provisioning and USB-free LAN 6-digit SRP6a setup',
        'Registers rodakos-xxxxxx.local mDNS automatically and confirms first-boot health markers'
      ],
      primaryLabel: 'Download Rodak Desktop',
      primaryTo: SITE_LINKS.rodakReleases,
      primaryExternal: true,
      secondaryLabel: 'Read Getting Started',
      secondaryTo: SITE_LINKS.docsStart
    },
    developer: {
      badge: 'Firmware Builders · ESP-IDF 6.0.2 + WSL Host Tests',
      title: 'Source Build, Signed Packaging & Host Regression',
      description:
        'Activate the pinned local toolchain in one line. Never run root idf.py flash on the Recovery layout — it targets the smaller 2.5 MiB factory partition.',
      icon: 'i-lucide-terminal',
      highlights: [
        'generate_board_config.ps1 owns Board Manager generation and path normalization',
        'flash_and_test.ps1 hashes Recovery before refreshing only otadata and ota_0',
        'Offline production signing keeps private keys completely off the build host',
        '64 host suites run under WSL/Linux with ASan/UBSan and no physical board required'
      ],
      primaryLabel: 'Read Build & Flash Guide',
      primaryTo: SITE_LINKS.docsFlash,
      secondaryLabel: 'Browse Source on GitHub',
      secondaryTo: SITE_LINKS.github,
      secondaryExternal: true
    },
    commandTabs: {
      firmwareLabel: 'ESP-IDF 6.0.2 Build & Flash',
      hostTestsLabel: 'WSL Host Tests',
      firmwareTitle: 'PowerShell · Pinned Toolchain Build & Guarded Refresh',
      hostTestsTitle: 'WSL / Linux · ASan/UBSan Host Regression',
      firmwareCode: `# 1. Activate project-local ESP-IDF 6.0.2 (no global profile needed)
. .\\activate_idf.ps1 -Version v6.0.2

# 2. First build (generates Board Manager tables and builds ota_0)
.\\build_rodakos.ps1

# 3. Verify partition table + Recovery hash, then refresh without erasing NVS
.\\flash_and_test.ps1 -Port COM3 -VerifyOnly
.\\flash_and_test.ps1 -Port COM3 -NoMonitor`,
      hostTestsCode: `# Run any host UI or model suite under WSL Debian / Linux
cmake -S /mnt/d/workspace/rodakos/tests/home_ui \\
      -B ~/.cache/rodakos-home-ui -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/rodakos-home-ui
ctest --test-dir ~/.cache/rodakos-home-ui --output-on-failure

# Full release host regression (requires reviewed ESP-IDF 6.0.2 tree)
export RODAKOS_IDF_PATH=/path/to/esp-idf-v6.0.2
bash tools/run_release_host_checks.sh "$HOME/.cache/rodakos-release"`,
      firmwareNote:
        'For the first migration onto the Recovery layout, pass -Erase explicitly (.\\flash_and_test.ps1 -Port COM3 -Erase -NoMonitor). Daily incremental edits use idf.py build.',
      hostTestsNote:
        'Host suites validate 64 UI, model, audio, voice, MQTT, and OTA targets under sanitizers, while hardware evidence remains authoritative for physical gates.'
    },
    copyLabel: 'Copy',
    copiedLabel: 'Copied',
    readinessTitle: 'Release Readiness & Open Production Gates',
    readinessBadge: 'Production Release: NO_GO',
    readinessDesc:
      'v0.1.6 passes hardware checks for Home, local wake, realtime voice, synced captions, LAN SRP6a onboarding, WebRTC streaming, and software-reset OTA matrices. Production release stays transparently NO_GO until these 3 physical gates close:',
    readinessGates: [
      {
        issue: '#30',
        url: SITE_LINKS.issueSigning,
        label: 'Production offline signing root & production-signed Rodak OTA'
      },
      {
        issue: '#29',
        url: SITE_LINKS.issuePowerCut,
        label: 'Real power-cut fixture across download, FAT flush/rename, flash & NVS writes'
      },
      {
        issue: '#28',
        url: SITE_LINKS.issueSoak,
        label: '8-hour qualification soak on an identified ordinary build'
      }
    ],
    readinessCta: 'Inspect Dated Hardware Evidence'
  },
  faq: {
    headline: 'Straight Engineering Answers',
    title: 'Clear boundaries on offline use, NO_GO status, flashing & non-goals',
    description:
      'Every capability is tracked by evidence level (Implemented, Host-verified, Hardware-verified) so you know exactly what is proven today.',
    items: [
      {
        label: 'What works on RodakOS without running a Rodak server?',
        icon: 'i-lucide-wifi-off',
        content:
          'Most apps run standalone offline: paged Home, Settings, 4 built-in themes, Clock, offline 1900–2100 lunar Calendar, Photos, MP3/WAV Music, 20-bar Recorder, GC0308 Camera capture, Files, QMI8658 Gyro level, early-boot USB MSC disk mode, and LAN Wake-on-LAN. Connecting to a local Rodak server unlocks conversational voice, Messages inbox sync, WebRTC display/camera sharing, live Appearance Studio publishing, and cloud-staged OTA.'
      },
      {
        label: 'Why is production release still marked NO_GO? Can makers flash it today?',
        icon: 'i-lucide-clipboard-check',
        content:
          'Yes — makers and firmware developers can build and run v0.1.6 today. Keeping production release at NO_GO reflects strict release discipline: we will not declare production readiness until offline production signing (#30), physical power-cut testing (#29), and an 8-hour ordinary-build soak (#28) have dated hardware evidence.'
      },
      {
        label: 'Why must I use flash_and_test.ps1 instead of root idf.py flash?',
        icon: 'i-lucide-shield-alert',
        content:
          'RodakOS partitions 16 MB flash into an immutable 2.5 MiB factory Recovery slot plus a single 13.3125 MiB ota_0 main slot. Root idf.py flash targets the small factory slot and breaks the layout. flash_and_test.ps1 verifies the installed partition table and Recovery hashes first, writes only otadata and ota_0, and validates first-boot confirmation logs.'
      },
      {
        label: 'Can RodakOS dynamically load third-party binary apps from the SD card?',
        icon: 'i-lucide-package-x',
        content:
          'No — runtime binary plugin loading and direct SD execution are explicit non-goals on ESP32-S3. Apps are modular at firmware architecture level through static registration. Any future untrusted MiniApp runtime would require a separate signed sandbox and capability broker instead of direct PhoneServices access.'
      },
      {
        label: 'Which boards are supported, and how can I port another ESP32-S3 board?',
        icon: 'i-lucide-circuit-board',
        content:
          'Only RYMCU BigSmart (ESP32-S3, 16 MB flash, 8 MB PSRAM) is currently supported and validated. Hardware configuration is isolated in components/rodakos_hal_boards/ with Espressif Board Manager, so new ports belong under boards/<vendor>/<board>/ with explicit build selection and hardware acceptance.'
      }
    ],
    ctaTitle: 'Own every line of firmware and every pixel on your desk companion',
    ctaDescription:
      'Clone rymcu/rodakos to run all 64 host tests locally, browse the architecture docs, or try the interactive protocol lab in your browser.',
    primaryCta: 'Explore on GitHub',
    secondaryCta: 'Read Documentation',
    tertiaryCta: 'Open Protocol Lab'
  },
  footer: {
    tagline:
      'Open-source local-first pocket "Phone OS" firmware for ESP32-S3, built for BigSmart-Open hardware and paired with the Rodak workbench.',
    copyright:
      'Copyright © 2026 RYMCU · RodakOS (Open-source firmware · Contracts and hardware evidence in GitHub are authoritative)',
    columns: [
      {
        label: 'Get Started',
        children: [
          { label: 'Introduction to RodakOS', to: SITE_LINKS.docsStart },
          { label: 'Build & Safe Flashing', to: SITE_LINKS.docsFlash },
          { label: 'BigSmart & 3D Enclosure', to: SITE_LINKS.docsHardware }
        ]
      },
      {
        label: 'Architecture & Protocols',
        children: [
          { label: 'Layers & Boot Order', to: SITE_LINKS.docsArchitecture },
          { label: 'Protocols Overview', to: SITE_LINKS.docsProtocols },
          { label: 'Protocol Handbook (14 Guides)', to: SITE_LINKS.docsHandbook },
          { label: 'Interactive Protocol Lab', to: SITE_LINKS.protocolsLabPath }
        ]
      },
      {
        label: 'Ecosystem & Repos',
        children: [
          { label: 'RodakOS on GitHub', to: SITE_LINKS.github, target: '_blank' },
          { label: 'BigSmart-Open Hardware', to: SITE_LINKS.hardware, target: '_blank' },
          { label: 'Rodak Desktop & NAS', to: SITE_LINKS.rodakReleases, target: '_blank' }
        ]
      }
    ]
  }
}
