import { SITE_LINKS } from '~/config/links'
import type { LandingCopy } from '~/types/landing'

export const LANDING_COPY_ZH: LandingCopy = {
  meta: {
    title: 'RodakOS — 面向 ESP32-S3 的开源本地优先掌上 “Phone OS”',
    description:
      '基于 ESP-IDF 6.0.2 与 LVGL 9.3 打造的开源 ESP32-S3 终端固件。在 320×240 触控屏上集成 16 款原生应用、离线 MultiNet 唤醒与逐帧字幕、免 USB 的 LAN 6 位码配网，以及不可变 Recovery 签名 OTA。'
  },
  nav: {
    links: [
      { label: '核心特性', to: '/#highlights', hash: '#highlights' },
      { label: 'Shell 与应用', to: '/#apps-shell', hash: '#apps-shell' },
      { label: '语音与协议', to: '/#voice', hash: '#voice' },
      { label: '硬件与生态', to: '/#hardware', hash: '#hardware' },
      { label: '构建与刷机', to: '/#build-flash', hash: '#build-flash' }
    ],
    docsLabel: '开发文档',
    protocolsLabel: '协议实验室',
    githubCta: 'GitHub',
    searchLabel: '搜索文档',
    localeToggleLabel: 'EN'
  },
  hero: {
    badge: 'ESP32-S3 开源微型 Phone OS · ESP-IDF 6.0.2 · LVGL 9.3 · v0.1.6',
    titleLine1: '把一块 320×240 的 ESP32-S3 屏幕，',
    titleLine2: '变成一台真正可控的掌上 “Phone OS”。',
    description:
      'RodakOS 是面向 RYMCU BigSmart 的开源终端固件实验。在 16 MB Flash 与 8 MB PSRAM 边界内，把多页触控桌面、16 款原生应用、本地离线唤醒与逐帧语音字幕、局域网可信配网以及不可变 Recovery 安全升级，收拢成一套清晰、克制、工程可验证的本地体验。',
    primaryCta: '查看源码仓库',
    secondaryCta: '体验协议实验室',
    docsCta: '阅读架构文档',
    hint: '开源固件 rymcu/rodakos · 64 套 WSL/Linux 主机端 ASan/UBSan 测试 · 原生协同 Rodak 本地工作台',
    previewTabs: [
      {
        id: 'home',
        label: '多页触控桌面',
        icon: 'i-lucide-layout-grid',
        title: 'Home 桌面 · 多页网格、文件夹、长按整理与 4 套内置主题',
        alt: 'RodakOS 真实 LVGL 9.3 渲染的 320×240 Home 桌面，展示 Ionicons 分类渐变圆角图标、顶部状态栏与多页指示器。',
        caption:
          '支持最多 8 个桌面页与只读 All Apps 页，仅常驻当前页与左右相邻页的 LVGL 控件；点击下方色点可实时切换深色 / 浅色 / 蓝色 / 绿色 4 套固件主题。',
        darkSrc: 'screenshots/home-dark.webp',
        lightSrc: 'screenshots/home-light.webp',
        supportsFirmwareThemes: true
      },
      {
        id: 'voice-caption',
        label: '全局语音字幕',
        icon: 'i-lucide-captions',
        title: 'Shell 全局底部语音栏 · 进度提示与逐帧同步回复字幕',
        alt: 'RodakOS 真实 LVGL 9.3 渲染的 Home 桌面与 Shell 顶部层磨砂质感语音字幕浮层。',
        caption:
          '语音栏由 Shell 统一挂载在 lv_layer_top()，横跨所有应用显示 turn.progress 思考步骤与绑定 RAV1 写入帧的回复字幕，遇到软键盘或投屏共享时自动礼让。',
        darkSrc: 'screenshots/voice-caption-dark.webp',
        lightSrc: 'screenshots/voice-caption-light.webp'
      },
      {
        id: 'calendar',
        label: '离线农历月历',
        icon: 'i-lucide-calendar-days',
        title: 'Calendar 日历 · 1900–2100 离线农历、节气与节日单屏月历',
        alt: 'RodakOS 真实 LVGL 9.3 渲染的 320×240 月历界面，每个日期格内排布公历与 12px 农历/节气小字。',
        caption:
          '内置离线 lunar_calendar 算法与专用 12 px 农历字库，单屏容纳 6 周月历并支持滑动切月、日视图与未校时状态下的应用内 SNTP 对时。',
        darkSrc: 'screenshots/calendar-dark.webp',
        lightSrc: 'screenshots/calendar-light.webp'
      },
      {
        id: 'gyro',
        label: '陀螺仪水平仪',
        icon: 'i-lucide-compass',
        title: 'Gyro 水平仪 · 154×154 RGB565 气泡表盘与六轴条图',
        alt: 'RodakOS 真实 LVGL 9.3 渲染的 Gyro 水平仪界面，左侧为 154×154 圆形气泡画布，右侧显示滚转、俯仰、偏航角度以及归零和定格按钮。',
        caption:
          'QMI8658 六轴采样在独立后台任务完成，绝不阻塞 LVGL 线程；支持相对归零、画面定格以及六轴实时居中条图切换。',
        darkSrc: 'screenshots/gyro-dark.webp',
        lightSrc: 'screenshots/gyro-light.webp'
      },
      {
        id: 'smart',
        label: '智能灯控',
        icon: 'i-lucide-lightbulb',
        title: 'Smart 智能家居 · 单字段原子写入与语音指示灯状态叠加',
        alt: 'RodakOS 真实 LVGL 9.3 渲染的 Smart 智能灯控总览界面，显示多盏灯光卡片、亮度与开关状态。',
        caption:
          '每次操作均以单字段 ApplyLightPatch 精确更新，亮度松手提交、关灯时选色不误开灯，并通过 500 ms 轮询同步 MQTT 变更与语音状态光。',
        darkSrc: 'screenshots/smart-dark.webp',
        lightSrc: 'screenshots/smart-light.webp'
      },
      {
        id: 'themes',
        label: '外观与发布信任',
        icon: 'i-lucide-palette',
        title: 'Themes 主题 · 配色预设、签名 SD 外观包与物理触控授权',
        alt: 'RodakOS 真实 LVGL 9.3 渲染的 Themes 外观设置界面，展示主题配色、外观包状态与物理确认发布者信任入口。',
        caption:
          '开机字标、壁纸与主题配色通过独立签名的 SD 外观包下发，首次信任发布者必须由本人在设备屏幕上物理触控确认，与固件 OTA 清晰隔离。',
        darkSrc: 'screenshots/themes-dark.webp',
        lightSrc: 'screenshots/themes-light.webp'
      }
    ],
    showcase: {
      realRenderBadge: '实时矢量可交互原型',
      demoDataNote:
        '原型屏幕与 3D 外壳模型均按真实 LVGL 9.3 布局、四套色板与实机交互重绘：顶部下滑开控制中心、桌面/日历左右滑翻页；实体键对应 POWER、IO0 BOOT（回桌面）与 IO10（单击控制中心 / 双击 Smart / 长按锁屏）。',
      themeLabel: '切换固件原生主题：',
      themeNames: {
        dark: '深色',
        light: '浅色',
        blue: '深蓝',
        green: '森绿'
      },
      expandLabel: '全屏交互体验',
      zoomInLabel: '放大至 1:1',
      fitLabel: '适应窗口',
      closeLabel: '关闭',
      mobileHint: '直接点击屏幕图标或在全屏中交互体验',
      prevSlideLabel: '上一张屏幕',
      nextSlideLabel: '下一张屏幕',
      carouselHint: '可直接点击任意图标与应用控件进行全屏矢量级交互体验。'
    }
  },
  highlights: {
    headline: '为什么是 RodakOS',
    title: '在 16 MB Flash 与 8 MB PSRAM 边界内，把可控与可靠做到实处',
    description:
      '拒绝拼凑 demo 式固件：从 HAL 抽象、内存预算、可信配网到 Recovery 救砖升级，每一层都有清晰的边界与主机/真机测试护航。',
    items: [
      {
        id: 'shell',
        icon: 'i-lucide-layers',
        badge: 'Phone OS Shell',
        title: '单一生命周期宿主与不打断应用的全局叠层',
        description:
          '静态注册表校验应用 ID 与别名唯一性；PhoneAppHost 独占 OnCreate/Resume/Pause/Destroy 生命周期，锁屏、控制中心与底部语音栏挂载于 lv_layer_top()，呼出不销毁当前应用。'
      },
      {
        id: 'on-demand',
        icon: 'i-lucide-cpu',
        badge: '按需硬件',
        title: '重硬件按需开启，退出即回收内部 DMA 内存',
        description:
          '音频编解码器、GC0308 相机、语音与 SD 卡均不在开机时常驻打开；常驻栈与 LVGL 堆迁入 PSRAM，本地/远程相机退出后最大连续内部 DMA 块回落恢复至 8 KiB 门槛。'
      },
      {
        id: 'wake',
        icon: 'i-lucide-mic',
        badge: '本地唤醒',
        title: '待机零云端连接，唤醒即全双工打断对话',
        description:
          '嵌入式 ESP-SR MultiNet 本地识别“你好达克”，前置唤醒期 AEC 与 AGC；待机不建立 WebSocket，唤醒后按需建连，独立播放任务保证扬声器阻塞永不拖慢上行打断。'
      },
      {
        id: 'trust',
        icon: 'i-lucide-shield-check',
        badge: '可信局域网',
        title: '屏显 6 位 SRP6a 码免 USB 首次建信与证书钉扎',
        description:
          '除 USB 写入外，支持在局域网中通过屏幕 6 位验证码（SRP6a）安全建立服务端身份；HTTPS、MQTTS 与 WSS 共享固定 .local 信任根，DNS-SD 候选路由经 TLS 核验才生效。'
      },
      {
        id: 'ota',
        icon: 'i-lucide-refresh-cw',
        badge: '不可变 Recovery',
        title: '2.5 MiB 不可变 Recovery + 13.3 MiB 单槽签名 OTA',
        description:
          'OTA 载荷暂存至 SD 卡而非切半浪费内部 Flash；独立的工厂 Recovery 是唯一允许改写 ota_0 的运行时，配合隔离 A/B 日志与每任务 RSA-2048 离线签名校验防止变砖。'
      },
      {
        id: 'verified',
        icon: 'i-lucide-flask-conical',
        badge: '64 套主机测试',
        title: '真实 LVGL 9.3 主机回归与依赖补丁哈希门禁',
        description:
          '64 套 WSL/Linux 主机测试直接编译生产级 UI、音频、MQTT、OTA 与语音源码并启用 ASan/UBSan 泄漏检查；托管组件补丁在 CMake 构建期核对上游源码哈希，杜绝静默漂移。'
      }
    ]
  },
  appsShell: {
    headline: 'Shell 机制与 16 款内置应用',
    title: '离线是一台独立的掌上工具机，连上 Rodak 是全屋语音伙伴',
    description:
      '全部 16 款内置应用采用静态模块化注册，共享统一主题色槽、布局辅助与路径租约安全边界；绝大多数日常与多媒体工具无需联网即可独立使用。',
    offlineBadge: '离线可用',
    cloudBadge: '协同 Rodak',
    features: [
      {
        icon: 'i-lucide-panels-top-left',
        title: '三页驻留渲染窗口（最多 8 页 + All Apps）',
        description:
          'Home 为每页保留轻量 tile 外壳，但仅保留当前页与左右相邻页的图标按钮树；滑动结束后异步释放远页控件，避免 25+ 应用场景下的内部内存峰值。'
      },
      {
        icon: 'i-lucide-hand',
        title: 'GT911 缓存轮询桥与零 I2C 渲染阻塞',
        description:
          '低优先级后台任务独立读取 GT911 触摸与 QMI8658 传感器并写入缓存，LVGL 渲染回调永不直接操作 I2C 总线，彻底规避无中断脚硬件上的触控死锁。'
      },
      {
        icon: 'i-lucide-sliders-horizontal',
        title: 'IO10 物理侧键快捷映射（NVS 可自定义）',
        description:
          '默认单击切换控制中心、双击直达 Smart 灯控、长按立即锁屏；自定义按键绑定保存在 NVS 并缓存于内存，与 PSRAM 栈任务安全隔离。'
      }
    ],
    categories: [
      {
        id: 'system',
        label: '系统与外壳',
        icon: 'i-lucide-layout-grid',
        sceneId: 'home-page2',
        screenTitle: 'Home 第二页 · Wake、Themes 与 Messages 入口及主题管理',
        screenCaption:
          'Home 支持文件夹、版本化精确 ID 布局与单次受保护保存的整理模式；Settings 与 Themes 管理网络、亮度、USB U 盘模式与签名外观资源。',
        screenAlt: 'RodakOS 真实 LVGL 9.3 渲染的 Home 第二页，展示 Wake、Themes 与 Messages 三个图标。',
        darkSrc: 'screenshots/home-page2-dark.webp',
        note: 'Home、Settings、Themes 与 System 构成基础终端体验，并由 Shell 提供锁屏与控制中心叠层。',
        apps: [
          {
            id: 'home',
            name: 'Home 桌面',
            icon: 'i-lucide-home',
            slotColor: '#5A6E8C',
            summary: '多页应用网格、文件夹、长按整理、未读红点与三页驻留窗口',
            offline: true
          },
          {
            id: 'settings',
            name: 'Settings 设置',
            icon: 'i-lucide-settings',
            slotColor: '#5A6E8C',
            summary: 'Wi-Fi 配网、背光亮度、一次性早启动 USB U 盘模式与 Web 文件管理',
            offline: true
          },
          {
            id: 'themes',
            name: 'Themes 主题',
            icon: 'i-lucide-palette',
            slotColor: '#5A6E8C',
            summary: '4 套原生配色预设、签名 SD 外观包状态与物理触控发布者信任确认',
            offline: true
          },
          {
            id: 'system',
            name: 'System 系统信息',
            icon: 'i-lucide-info',
            slotColor: '#5A6E8C',
            summary: '固件版本、Wi-Fi、内部 SRAM / PSRAM 余量与存储状态一目了然',
            offline: true
          }
        ]
      },
      {
        id: 'voice-smart',
        label: '语音与智能',
        icon: 'i-lucide-sparkles',
        sceneId: 'assistant',
        screenTitle: 'Assistant 语音助手 · 唤醒状态、实时会话阶段与云端自检修复',
        screenCaption:
          'Assistant 以系统级服务运行而非绑定应用生命周期；同组还包含 Smart 灯控、Messages 消息待办中心与 Wake 局域网唤醒。',
        screenAlt: 'RodakOS 真实 LVGL 9.3 渲染的 Assistant 应用界面，展示本地唤醒开关、语音指示灯与云端连接诊断状态卡。',
        darkSrc: 'screenshots/assistant-dark.webp',
        note: 'Wake-on-LAN 仅需局域网 Wi-Fi；语音对话、消息待办与跨设备联动通过钉扎 TLS 协同 Rodak。',
        apps: [
          {
            id: 'assistant',
            name: 'Assistant 语音助手',
            icon: 'i-lucide-message-circle',
            slotColor: '#23919F',
            summary: '本地唤醒控制、会话状态卡、语音状态灯与云端故障一键修复引导',
            offline: false
          },
          {
            id: 'smart',
            name: 'Smart 智能灯控',
            icon: 'i-lucide-lightbulb',
            slotColor: '#23919F',
            summary: '单灯控制与多灯总览，单字段 Patch 原子写入，同步 MQTT 与语音灯态',
            offline: false
          },
          {
            id: 'messages',
            name: 'Messages 消息中心',
            icon: 'i-lucide-bell',
            slotColor: '#C23A6E',
            summary: '拉取通知与待办提醒，支持屏上完成或推迟 10/30/60 分钟，投屏自动隐藏',
            offline: false
          },
          {
            id: 'wol',
            name: 'Wake 网络唤醒',
            icon: 'i-lucide-power',
            slotColor: '#C23A6E',
            summary: '持久化最多 8 台局域网主机，发送校验后的 UDP Wake-on-LAN 魔术包',
            offline: true
          }
        ]
      },
      {
        id: 'media',
        label: '多媒体',
        icon: 'i-lucide-disc-3',
        sceneId: 'recorder',
        screenTitle: 'Recorder 录音机 · 20 柱 48 dB 实时电平表与原子 WAV 落盘',
        screenCaption:
          '录音、音乐、相册与相机均按需占用编解码器、DVP 与 FileService 路径租约；录音仅在 WAV 头回写、flush 与 close 全部成功后才报告已保存。',
        screenAlt: 'RodakOS 真实 LVGL 9.3 渲染的 Recorder 录音界面，显示计时器、20 柱实时输入电平表与快门式录音按钮。',
        darkSrc: 'screenshots/recorder-dark.webp',
        note: '多媒体应用均优先利用 PSRAM 缓冲与路径级写租约，区分“空库”与“读卡失败”并支持原地重试。',
        apps: [
          {
            id: 'recorder',
            name: 'Recorder 录音机',
            icon: 'i-lucide-mic',
            slotColor: '#D0644E',
            summary: '20 柱 48 dB 实时电平表、扬声器自测、即时回放与安全 WAV 封尾校验',
            offline: true
          },
          {
            id: 'music',
            name: 'Music 音乐',
            icon: 'i-lucide-music',
            slotColor: '#D0644E',
            summary: '扫描 /music 播放 16-bit PCM WAV 与 MP3，异步扫描不阻塞触控回调',
            offline: true
          },
          {
            id: 'camera',
            name: 'Camera 相机',
            icon: 'i-lucide-camera',
            slotColor: '#D0644E',
            summary: '按需开启 GC0308 DVP 预览拍照并支持 WebRTC 远程视频流互斥管理',
            offline: true
          },
          {
            id: 'photos',
            name: 'Photos 相册',
            icon: 'i-lucide-image',
            slotColor: '#B87A22',
            summary: '扫描 /photos 与 /DCIM 浏览 JPG/PNG/BMP，优先使用 SPIRAM 加载缩略图',
            offline: true
          }
        ]
      },
      {
        id: 'tools',
        label: '效率工具',
        icon: 'i-lucide-wrench',
        sceneId: 'wol',
        screenTitle: '效率工具组 · 农历日历、水平仪、时钟、SD 文件管理与网络唤醒',
        screenCaption:
          '全部针对 320×240 电容触控屏重新打磨信息层级与点击热区；Files 在删除前执行确认弹窗且严格禁止删除 /rodakos 系统目录。',
        screenAlt: 'RodakOS 真实 LVGL 9.3 渲染的 Wake-on-LAN 主机列表界面，卡片左侧编辑、右侧独立电源键发送唤醒包。',
        darkSrc: 'screenshots/wol-dark.webp',
        lightSrc: 'screenshots/wol-light.webp',
        note: '所有工具均离线可用；日历与时钟在联网后可一键触发应用内 SNTP 校时。',
        apps: [
          {
            id: 'calendar',
            name: 'Calendar 农历日历',
            icon: 'i-lucide-calendar',
            slotColor: '#3D7DD3',
            summary: '1900–2100 离线农历、二十四节气与节日单屏月历，支持日视图与滑动切月',
            offline: true
          },
          {
            id: 'gyro',
            name: 'Gyro 陀螺仪',
            icon: 'i-lucide-compass',
            slotColor: '#3D7DD3',
            summary: '154×154 RGB565 气泡水平仪（归零/定格）与六轴居中条图，失败可重试',
            offline: true
          },
          {
            id: 'clock',
            name: 'Clock 时钟',
            icon: 'i-lucide-clock',
            slotColor: '#B87A22',
            summary: '本地大字时钟显示与网络 SNTP 校时入口，支持时区持久化',
            offline: true
          },
          {
            id: 'files',
            name: 'Files 文件管理',
            icon: 'i-lucide-folder',
            slotColor: '#7A7068',
            summary: 'SD 目录浏览、全屏看图、异步空间统计与受保护的确认删除机制',
            offline: true
          }
        ]
      }
    ]
  },
  voice: {
    headline: '固件端语音链路与开放协议',
    title: '从本地唤醒到逐帧字幕，每一条通道都有机器可读契约',
    description:
      'RodakOS 的实时语音不是黑盒私有流：完整的 rodak-aiot/v1 与 rodak-realtime-voice/v1 契约均以 OpenAPI / AsyncAPI / JSON Schema 形式开源在仓库中，配套 174 条测试样例与浏览器交互实验室。',
    diagramBadge: '固件侧 5 阶段 · Firmware Pipeline',
    diagramNote:
      '注：待机期间仅在 ESP32-S3 本机运行 MultiNet 唤醒检测，不维持语音 WebSocket；完整语音对话与播报需配合运行 Rodak 服务端。',
    labCta: '打开协议交互实验室',
    stages: [
      {
        id: 'wake',
        step: '01',
        title: '本地唤醒',
        engine: 'ESP-SR MultiNet + AEC/AGC',
        metric: '待机零网络',
        icon: 'i-lucide-ear',
        summary: '在芯片端持续运行低功耗本地唤醒词“你好达克”，前置同步回声消除与自动增益控制，命中后播放唤醒提示音。',
        highlights: [
          '待机态无 VoiceWs 连接，隐私与带宽占用均为零',
          '检测窗口在静音段自动重置，并受共享音频焦点协调',
          '高音量播放时为 ES7210 保留 ADC 余量，降低自激误判'
        ]
      },
      {
        id: 'connect',
        step: '02',
        title: '安全建连',
        engine: 'Pinned WSS + HS256 JWT',
        metric: '600s 自动续期',
        icon: 'i-lucide-lock',
        summary: '唤醒命中后抢占音频焦点并按需建立钉扎 TLS WebSocket 会话，自动在 600 秒令牌到期前用配对密钥静默刷新。',
        highlights: [
          '与 HTTPS / MQTTS 共用同一可信服务端证书与访问令牌',
          '若远程相机 WebRTC 流正在运行，语音会话先有序停止推流再建连',
          '刷新被拒时直接结束本次尝试，绝不擅自重置配对身份'
        ]
      },
      {
        id: 'duplex',
        step: '03',
        title: '双工打断',
        engine: 'RAV1 帧 + 双任务解耦',
        metric: 'Core 1 独立播放',
        icon: 'i-lucide-audio-waveform',
        summary: 'assistant_io 负责收发与解码并写入 50 帧队列（约 3 秒），独立 assistant_play 任务（6 KiB PSRAM 栈）写向 ES8311 扬声器。',
        highlights: [
          '扬声器写阻塞永不影响上行采集，开口即可触发设备端打断',
          '四槽 16-bit 固定 I2S TX 帧格式，避免切换声道撕裂内部 DMA 连续块',
          '四道消息门控（generation / sessionId / playbackEpoch / sequence）拦截陈旧音频帧'
        ]
      },
      {
        id: 'caption',
        step: '04',
        title: '逐帧字幕',
        engine: 'turn.progress + output.caption',
        metric: 'Shell 顶层浮层',
        icon: 'i-lucide-captions',
        summary: '思考与工具阶段在 Shell 底部浮层展示 turn.progress，回复阶段按 RAV1 写入帧序号精确切换 output.caption 字幕。',
        highlights: [
          '磨砂质感浮层挂载在 lv_layer_top()，横跨任意应用可见',
          '轻触即可临时隐藏当前字幕，遇软键盘、锁屏或屏幕共享自动礼让',
          '多轮连续追问复用同一会话，说“再见”或 30 秒静音后有序退回本地唤醒'
        ]
      },
      {
        id: 'announce',
        step: '05',
        title: '主动播报',
        engine: 'MQTT voice.announce',
        metric: '零 MCP 攻击面',
        icon: 'i-lucide-megaphone',
        summary: '服务端可通过 MQTT 下发 voice.announce 触发纯播报会话；固件不注册 MCP 工具，音量与灯光走带关联回执的 MQTT 效果通道。',
        highlights: [
          'MQTT 线程仅入队并回 ACK，由语音前端命令任务独立拉起播放会话',
          '音量与灯光变更绑定原始连接纪元（epoch），不盲目重放过期指令',
          '如实区分“软件回执已确认”与“物理硬件结果已验证”'
        ]
      }
    ],
    cards: [
      {
        icon: 'i-lucide-network',
        title: '三条通道，一个访问令牌',
        description:
          'HTTP 负责身份、配对、续期与 OTA 下载；MQTT 负责遥测、影子、命令与效果；WebSocket 仅在唤醒或播报时按需建立，三通道共享配对签发的 HS256 JWT。'
      },
      {
        icon: 'i-lucide-history',
        title: '64 条权威防重放账本',
        description:
          '命令结果保留最近 64 条不可变重放缓存并校验连接纪元，旧连接回调与过期队列在切换时自动丢弃，契约边界可在浏览器里逐条验证。'
      },
      {
        icon: 'i-lucide-file-code-2',
        title: 'OpenAPI / AsyncAPI 机器可读规范',
        description:
          '所有 HTTP、MQTT 与实时语音帧格式均附带 JSON Schema 与 174 条正/反例 Fixtures，方便你编写兼容设备、模拟器或第三方服务端。'
      }
    ]
  },
  hardware: {
    headline: '硬件平台、3D 外壳与双端生态',
    title: '软硬彻底解耦，从开源开发板到可打印桌面成品一气呵成',
    description:
      '硬件抽象由 RodakOS 自维护的 rodakos_hal_boards 与 Espressif Board Manager 驱动；搭配 BigSmart-Open 主板与开源 build123d 参数化 3D 外壳，以及 Rodak 本地工作台，形成完整的个人 AIoT 闭环。',
    projects: [
      {
        id: 'hardware',
        badge: '开源硬件 · Open Hardware',
        repoLabel: 'rymcu/BigSmart-Open',
        title: 'BigSmart-Open 参考开发板',
        description:
          'RodakOS 当前官方验证支持的参考板卡。基于 ESP32-S3-WROOM-1-N16R8，集成 320×240 ST7789 电容触摸屏、ES8311/ES7210 双音频编解码、GC0308 摄像头、QMI8658 六轴 IMU、PCA9557 IO 扩展与 WS2812 RGB 灯。',
        icon: 'i-lucide-circuit-board',
        to: SITE_LINKS.hardware,
        specs: [
          'ESP32-S3 · 16MB Flash / 8MB PSRAM',
          '320×240 ST7789 + GT911 触控',
          'ES8311 + ES7210 双麦与扬声器',
          'GC0308 DVP + QMI8658 + SDMMC'
        ],
        ctaLabel: '访问 BigSmart-Open 仓库'
      },
      {
        id: 'rodak',
        badge: '配套本地工作台 · Companion Workbench',
        repoLabel: 'rymcu/rodak-releases',
        title: 'Rodak 桌面与 NAS 服务端',
        description:
          '运行在你自己电脑或 NAS 上的本地优先工作台：提供可视化串口刷机、USB 一键可信配网、320×240 外观工坊实时编译预览、离线 SenseVoice/Kokoro 语音引擎以及 WebRTC 屏幕远程同传。',
        icon: 'i-lucide-monitor',
        to: SITE_LINKS.rodakReleases,
        specs: [
          'Windows 桌面 + NAS Docker',
          'USB 可信配网与可视化烧录',
          '320×240 外观工坊与签名下发',
          'WebRTC 屏幕同传与远程触控'
        ],
        ctaLabel: '前往 Rodak Releases'
      }
    ],
    enclosureHeadline: '开源结构工程与存储架构',
    enclosureTitle: 'BigSmart-Open 全参数 build123d 3D 外壳，配合 16 MB 单大槽 Recovery 分区',
    enclosureDescription:
      'BigSmart-Open 硬件仓库配套开源面向双麦声学隔离与 FDM 免支撑打印的 6 件套参数化外壳（含 15° 桌面底座）；固件底层 Flash 分区则通过 SD 卡暂存 OTA，释放 13.3125 MiB 给主程序与本地语音模型。',
    enclosureCta: '查看硬件与 3D 外壳指南',
    enclosureFeatures: [
      {
        icon: 'i-lucide-box',
        title: '6 个免支撑打印件 + 可分离 15° 桌面底座',
        description:
          '机身 106.0 × 86.0 × 33.2 mm，B-rep 全量零干涉校验，含双麦气密导声台、3525 喇叭隔音围墙与 0.60 mm 隐形柔光 RGB 仓。'
      },
      {
        icon: 'i-lucide-wrench',
        title: '全机仅需 M2 / M3 两款常见自攻螺丝',
        description:
          '19 颗螺丝规范统一；修改 params.py 即可按手头螺丝长度自动校核壁厚并一键重导出 STL / STEP。'
      },
      {
        icon: 'i-lucide-hard-drive',
        title: '面向未来板卡的 Board Manager HAL 规范',
        description:
          '设备与引脚配置全数收敛在 components/rodakos_hal_boards/，社区移植新板卡只需遵循 boards/<vendor>/<board>/ 显式构建选型。'
      }
    ],
    partitionTitle: '16 MB Flash 分区布局（partitions_16m.csv）',
    partitionBadge: '16 MiB · SD-Staged OTA',
    partitionSummary:
      '常规双槽 A/B 分区在 16 MB Flash 上只能给每个槽位约 7 MB；RodakOS 通过 SD 卡暂存并验签 OTA 包，将 13.3125 MiB 完整留给主程序与嵌入式语音模型，同时保留 2.5 MiB 不可变 Recovery 守护救砖底线。',
    partitions: [
      {
        name: 'sys',
        subType: 'nvs / otadata / ota_state',
        offset: '0x9000',
        sizeLabel: '60 KiB',
        widthPercent: 8,
        colorClass: 'bg-slate-600',
        role: 'NVS 配置、OTA 引导选择、PHY 参数与独立隔离的 A/B OTA 日志状态分区。'
      },
      {
        name: 'recovery',
        subType: 'app / factory',
        offset: '0x20000',
        sizeLabel: '2.5 MiB',
        widthPercent: 20,
        colorClass: 'bg-amber-600',
        role: '独立编译的不可变工厂 Recovery，唯一允许校验并重写 ota_0 主槽的运行时。'
      },
      {
        name: 'app (ota_0)',
        subType: 'app / ota_0',
        offset: '0x2a0000',
        sizeLabel: '13.3125 MiB',
        widthPercent: 66,
        colorClass: 'bg-sky-600',
        role: '主应用槽位：完整承载 RodakOS 16 款应用、LVGL 9.3、中文字库与本地 MultiNet 唤醒模型。'
      },
      {
        name: 'core',
        subType: 'data / coredump',
        offset: '0xff0000',
        sizeLabel: '64 KiB',
        widthPercent: 6,
        colorClass: 'bg-rose-600',
        role: '异常现场转储分区，配合 LVGL assert panic 捕获留存可追溯调用栈。'
      }
    ],
    enclosureImageCaption:
      '来自 BigSmart-Open/enclosure 的参数化装配爆炸图与 15° 桌面底座视角（build123d 自动校验零干涉）。',
    enclosureImageAlt: 'RodakOS build123d 全参数化 3D 打印外壳爆炸装配图与 15 度桌面底座渲染图。'
  },
  buildFlash: {
    headline: '上手与源码构建',
    title: '可视化配网一键上手，或从 ESP-IDF 6.0.2 源码完整复现',
    description:
      '项目脚本严格锁定 ESP-IDF 6.0.2 与配套 Xtensa GCC，无需配置全局 PowerShell profile；刷机脚本在写盘前自动核验分区表与不可变 Recovery 哈希。',
    quickstart: {
      badge: '创客与玩家推荐 · 免装编译工具链',
      title: '搭配 Rodak 可视化刷机与配网',
      description:
        '插上 USB-C 线，在 Rodak 桌面端图形界面完成固件刷写与局域网信任根注入；也可刷机后直接在设备屏显 6 位码通过 LAN 完成免 USB 首次绑定。',
      icon: 'i-lucide-usb',
      highlights: [
        '首次迁移 Recovery 布局烧录合并镜像，后续常规升级仅推送 build/rodakos.bin',
        '支持 USB 可信配网与屏幕 6 位 SRP6a 验证码免 USB 局域网配网两种方式',
        '重启接入 Wi-Fi 后自动通过 mDNS（rodakos-xxxxxx.local）上线并完成首启确认'
      ],
      primaryLabel: '获取 Rodak 桌面端',
      primaryTo: SITE_LINKS.rodakReleases,
      primaryExternal: true,
      secondaryLabel: '查看刷机与配网文档',
      secondaryTo: SITE_LINKS.docsStart
    },
    developer: {
      badge: '固件开发者 · ESP-IDF 6.0.2 + WSL 主机测试',
      title: '源码构建、签名打包与主机端回归',
      description:
        '使用项目本地激活器一键进入锁定工具链；切勿对 Recovery 分区布局执行根 idf.py flash（会误写入 2.5 MiB factory 分区）。',
      icon: 'i-lucide-terminal',
      highlights: [
        'generate_board_config.ps1 自动生成并规范化 Board Manager 代码',
        'flash_and_test.ps1 先核验 Recovery 哈希再仅刷新 otadata 与 ota_0',
        '离线生产签名支持私钥永不接触构建机（-OfflineSigning）',
        '64 套主机端测试可在 WSL/Linux 下开启 ASan/UBSan 脱离开发板运行'
      ],
      primaryLabel: '阅读完整构建手册',
      primaryTo: SITE_LINKS.docsFlash,
      secondaryLabel: '查看 GitHub 源码',
      secondaryTo: SITE_LINKS.github,
      secondaryExternal: true
    },
    commandTabs: {
      firmwareLabel: 'ESP-IDF 6.0.2 构建与刷机',
      hostTestsLabel: 'WSL 主机端测试',
      firmwareTitle: 'PowerShell · 锁定工具链构建与安全刷新',
      hostTestsTitle: 'WSL / Linux · ASan/UBSan 主机回归测试',
      firmwareCode: `# 1. 激活项目本地 ESP-IDF 6.0.2（无需修改全局 profile）
. .\\activate_idf.ps1 -Version v6.0.2

# 2. 首次构建（自动生成 Board Manager 配置并编译 ota_0）
.\\build_rodakos.ps1

# 3. 核验分区表与不可变 Recovery 哈希，再无擦除刷新主应用
.\\flash_and_test.ps1 -Port COM3 -VerifyOnly
.\\flash_and_test.ps1 -Port COM3 -NoMonitor`,
      hostTestsCode: `# 在 WSL Debian / Linux 下独立运行任意主机端 UI 或模型测试套件
cmake -S /mnt/d/workspace/rodakos/tests/home_ui \\
      -B ~/.cache/rodakos-home-ui -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/rodakos-home-ui
ctest --test-dir ~/.cache/rodakos-home-ui --output-on-failure

# 全量发布回归（需指定已审阅的 ESP-IDF 6.0.2 源码目录）
export RODAKOS_IDF_PATH=/path/to/esp-idf-v6.0.2
bash tools/run_release_host_checks.sh "$HOME/.cache/rodakos-release"`,
      firmwareNote:
        '首次将旧设备迁移到 Recovery 分区布局时请显式使用 .\\flash_and_test.ps1 -Port COM3 -Erase -NoMonitor；日常增量改代码仅需运行 idf.py build。',
      hostTestsNote:
        '主机测试覆盖 64 套 UI、模型、音频、语音状态机、MQTT 与 OTA 安全套件；但主机测试不替代物理硬件验收门禁。'
    },
    copyLabel: '复制命令',
    copiedLabel: '已复制',
    readinessTitle: '当前发布门禁状态透明公示',
    readinessBadge: 'Production Release: NO_GO',
    readinessDesc:
      'v0.1.6 已在 BigSmart 真机验证多页桌面、本地唤醒与语音对话、逐帧字幕、LAN SRP6a 配网、WebRTC 同传及软件重启 OTA 矩阵；但本着工程诚实原则，在以下 3 项物理发布门禁取得实测证据前，生产发布保持 NO_GO：',
    readinessGates: [
      {
        issue: '#30',
        url: SITE_LINKS.issueSigning,
        label: '生产离线签名根部署与正式签名 OTA 全闭环'
      },
      {
        issue: '#29',
        url: SITE_LINKS.issuePowerCut,
        label: '下载 / FAT 刷盘 / Flash 与 NVS 写入阶段真实物理断电矩阵'
      },
      {
        issue: '#28',
        url: SITE_LINKS.issueSoak,
        label: '普通正式构建包的 8 小时连续资格烤机与内存门槛校验'
      }
    ],
    readinessCta: '查看带日期的硬件验证证据'
  },
  faq: {
    headline: '上手前讲清楚',
    title: '关于开源范围、离线边界、刷机规则与路线图的坦诚回答',
    description: '我们用证据级别（Implemented / Host-verified / Hardware-verified）描述每一项能力，不夸大未完成的物理验证。',
    items: [
      {
        label: '没有电脑端 Rodak 服务端时，刷了 RodakOS 的 BigSmart 能干什么？',
        icon: 'i-lucide-wifi-off',
        content:
          '完全可以独立运行。多页触控桌面、设置、本地 4 套配色主题、时钟、1900–2100 离线农历日历、相册、MP3/WAV 音乐播放、20 柱电平表录音、GC0308 相机拍照、SD 文件管理、QMI8658 气泡水平仪、USB U 盘模式与局域网 Wake-on-LAN 均不依赖 Rodak。只有在需要实时语音对话、消息中心待办同步、WebRTC 远程画面同传、在线外观编译与云端 OTA 时，才需连接局域网内的 Rodak 服务端。'
      },
      {
        label: '为什么强调生产发布仍为 NO_GO？现在适合创客和开发者刷来体验吗？',
        icon: 'i-lucide-clipboard-check',
        content:
          '非常适合创客折腾和二次开发。v0.1.6 的核心功能均已跑通真机与 64 套主机端测试；标注 NO_GO 是因为 RodakOS 对“生产级可靠”设定了严格的物理验收门槛——必须完成生产离线签名根部署（#30）、真实可控断电治具矩阵（#29）与 8 小时普通构建烤机（#28）才会摘除 NO_GO 标签。'
      },
      {
        label: '为什么不能直接跑根 idf.py flash，而要用 flash_and_test.ps1？',
        icon: 'i-lucide-shield-alert',
        content:
          'RodakOS 采用“2.5 MiB 不可变 Recovery（factory 分区）+ 13.3125 MiB 主槽（ota_0）”布局。根目录直接运行 idf.py flash 会试图把超过 6 MiB 的主程序刷进仅 2.5 MiB 的 factory 分区。flash_and_test.ps1 会先只读核验已安装分区表与 Recovery 哈希，再安全写入 otadata 与 ota_0，并自动抓包确认首启健康日志与本地 OTA 确认标记。'
      },
      {
        label: '支持在 ESP32-S3 上从 SD 卡动态加载第三方二进制 App 或插件吗？',
        icon: 'i-lucide-package-x',
        content:
          '不支持，这也是文档中明确写明的 Non-Goals。当前全部应用以静态链接方式注册到 PhoneAppRegistry，确保确定性的内存与生命周期边界；未来如果引入第三方 MiniApp，将设计独立签名沙箱与能力代理，不会把原生 PhoneServices 直接暴露给未受信任代码。'
      },
      {
        label: '目前支持哪些硬件开发板？可以移植到其他 ESP32-S3 板子吗？',
        icon: 'i-lucide-circuit-board',
        content:
          '当前唯一官方支持并验证的硬件是 RYMCU BigSmart（ESP32-S3，16 MB Flash，8 MB PSRAM）。由于底层已全面迁移至 rodakos_hal_boards + Espressif Board Manager，社区开发者可以在 boards/<vendor>/<board>/ 下新增板级 YAML 与适配器，通过显式构建选型和硬件验收流程完成新板卡移植。'
      }
    ],
    ctaTitle: '把掌上开源终端的每一行代码与每一颗像素，稳稳握回自己手里',
    ctaDescription:
      '克隆 rymcu/rodakos 源码立即在 WSL 跑起 64 套主机测试，或打开协议交互实验室亲手拆解 RAV1 音频帧与 OTA 签名校验。',
    primaryCta: '访问 GitHub 仓库',
    secondaryCta: '阅读开发文档',
    tertiaryCta: '体验协议交互实验室'
  },
  footer: {
    tagline:
      '面向 ESP32-S3 的开源本地优先掌上 “Phone OS” 固件，原生适配 BigSmart-Open 硬件并协同 Rodak 本地工作台。',
    copyright: 'Copyright © 2026 RYMCU · RodakOS（开源固件 · 契约文件与硬件证据以 GitHub 仓库为准）',
    columns: [
      {
        label: '快速上手',
        children: [
          { label: '认识 RodakOS', to: SITE_LINKS.docsStart },
          { label: '源码构建与安全刷机', to: SITE_LINKS.docsFlash },
          { label: 'BigSmart 硬件与 3D 外壳', to: SITE_LINKS.docsHardware }
        ]
      },
      {
        label: '架构与开放协议',
        children: [
          { label: '三层架构与启动顺序', to: SITE_LINKS.docsArchitecture },
          { label: '协议体系总览', to: SITE_LINKS.docsProtocols },
          { label: '协议开发者手册（14 篇）', to: SITE_LINKS.docsHandbook },
          { label: '协议交互实验室', to: SITE_LINKS.protocolsLabPath }
        ]
      },
      {
        label: '生态与源码仓库',
        children: [
          { label: 'RodakOS GitHub 源码', to: SITE_LINKS.github, target: '_blank' },
          { label: 'BigSmart-Open 开源硬件', to: SITE_LINKS.hardware, target: '_blank' },
          { label: 'Rodak 桌面与 NAS 工作台', to: SITE_LINKS.rodakReleases, target: '_blank' }
        ]
      }
    ]
  }
}
