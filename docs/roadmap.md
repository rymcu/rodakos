# RodakOS 项目规划

RodakOS 是面向 ESP32-S3 触摸设备的嵌入式 Phone OS 实验项目，不是 Web 原型。它的核心目标是把 `phone os` 和 `phone ui framework` 拆开：OS 管应用模型、生命周期、导航和系统服务；UI Framework 管 LVGL 上的主题、布局、控件和应用界面构建。

## 产品方向

RodakOS 的 Home 页应接近真实手机桌面，而不是调试页面或简单 launcher 列表。首屏需要承载系统状态、时间、应用入口、dock、页面感和触摸交互，并逐步达到 xiaozhi 级别的完整度与精致度。

第一阶段的“app 可拔插”不是运行时下载二进制插件。ESP32-S3 首期仍采用静态链接固件，app 通过 `PhoneAppDescriptor`、`PhoneAppRegistry`、factory callback 和 app id/alias 接入系统。Home 根据 registry 生成应用入口，OS core 不直接 include 具体 app 头文件。

## 当前基线

截至 2026-06-27，RodakOS 已具备以下基线：

- ESP-IDF 工程已创建，target 为 `esp32s3`。
- 已完成 ST7789 显示、GT911 触摸、PCA9557、GPIO42 背光的基础 bring-up。
- 已建立 `phone_os` 层：app descriptor、registry、host、navigation、services、system。
- 已建立 `phone_ui` 层：theme、screen、components、toast。
- 已实现 Home 桌面和 Settings app。
- 已在 `COM3` 成功 build + flash，并通过启动日志确认 Home 桌面拉起。

## Milestone 0：硬件与工程基线

目标：保证 RodakOS 是稳定可编译、可烧录、可启动的 ESP32-S3 固件项目。

已完成：

- 建立 `D:\workspace\rodakos` ESP-IDF 项目。
- 配置 `sdkconfig.defaults`，启用必要 LVGL 字体并关闭 demo/example。
- 验证 `idf.py build`。
- 验证 `idf.py -p COM3 flash`。
- 验证启动日志、触摸初始化和 Home 桌面启动。

后续补强：

- 固定板级文档，包括屏幕方向、触摸坐标、背光范围和 I2C 地址。
- 增加最小硬件自检页面或串口诊断命令。
- 记录固件大小、水位和分区策略。

## Milestone 1：Home 桌面 + 设置 App

目标：实现首个真正可用的 phone shell。

范围：

- Home 是全屏桌面，不使用普通 app chrome。
- Home 从 `PhoneAppRegistry` 读取可见 app 并生成图标入口。
- Settings app 支持亮度、主题、语言等基础偏好。
- 亮度通过 `PhoneServices -> Backlight` 生效。
- app 启动通过 app id/alias，不写中心化 switch。

验收标准：

- 设备上电后进入 Home 桌面。
- Home 显示系统状态、时间日期、app grid、dock/page indicator。
- 点按 Settings 可进入设置。
- 在 Settings 调整亮度后能立即作用到背光，并持久化到 NVS。
- 返回 Home 后桌面状态正常，不出现残留控件或黑屏。

## Milestone 2：Phone UI Framework 成型

目标：让 UI 框架不只是 demo helper，而是可支撑多个系统 app 的一致设计语言。

计划：

- 明确主题 token：颜色、字号、圆角、间距、状态色。
- 抽象手机常用控件：状态栏、列表项、开关、滑杆、分段控制、底部操作区、toast。
- 建立页面导航转场和返回行为。
- 让 Home、Settings 使用同一套触摸尺寸和视觉密度规则。
- 增加低内存设备上的 UI 创建/销毁约束，避免隐藏页面长期占用对象。

验收标准：

- 新增系统 app 时不需要重新写基础页面结构。
- 常用设置项可以用 framework 控件快速组合。
- 320x240 屏幕上文本不溢出，触摸目标尺寸稳定。

## Milestone 3：系统服务与桌面体验

目标：让桌面状态从静态展示变成真实系统状态。

计划：

- 增加 Wi-Fi 状态服务。
- 增加电量/充电状态服务，如果硬件支持。
- 增加时间服务和时间同步策略。
- Home 支持壁纸、图标资产、桌面分页和滑动手势。
- 增加通知或轻量状态事件入口。
- Settings 增加网络、显示、声音、关于设备等页面。

验收标准：

- Home 顶部状态不再使用假值。
- 桌面可以容纳多于一页 app。
- Settings 能覆盖最常用设备配置。

## Milestone 4：App 模型扩展

目标：把“静态链接但可拔插”的 app 模型扩展到更清晰的包边界。

计划：

- 统一 app manifest 字段，支持名称、图标、分类、权限、可见性、alias。
- 建立 built-in app 注册清单，减少手写注册分散度。
- 定义 app service API：设置、存储、网络、音频、系统事件。
- 探索组件级 app package 或 manifest 生成，但仍以 ESP-IDF 静态链接为默认路径。

验收标准：

- 新增 app 主要改 app 自身目录和注册清单。
- Home、Settings、OS core 不需要为每个新 app 增加专用分支。
- app 权限和资源需求可以被系统层看见。

## Milestone 5：xiaozhi 集成与系统应用

目标：在保持 RodakOS 架构清晰的前提下，接入 xiaozhi 能力或复用其成熟模块。

可能范围：

- 音频输入输出服务。
- 语音助手 app 或系统级 assistant surface。
- 网络配置和配网流程。
- OTA 或固件版本检查。
- 设备信息、日志导出和调试面板。

原则：

- xiaozhi 能力应作为 app 或 service 接入 Phone OS，而不是重新把所有逻辑塞回中心 `Application`。
- Home 继续作为真实 phone desktop 发展，不退化成调试菜单。
- 优先复用硬件驱动和成熟系统能力，避免复制一套不可维护的分叉。

## 近期任务清单

优先级从高到低：

1. 继续打磨 Home 桌面的视觉与触摸体验：图标、dock、页面感、状态区域。
2. 补齐 Settings 的显示设置与关于设备页面。
3. 增加真实时间服务，替换 Home 上的临时状态显示。
4. 增加 Wi-Fi 状态读取，并规划网络设置入口。
5. 整理 app manifest/registry 的字段，准备接入更多内置 app。
6. 为关键 UI 生命周期增加最小测试或串口诊断日志。

## 非目标

当前阶段不做：

- Web/Vite/React 原型。
- ESP32-S3 上的运行时动态二进制插件加载。
- 大而全的通用 OS 抽象。
- 在 Home 中硬编码所有 app 入口和启动逻辑。

RodakOS 第一阶段要先成为一个能在板子上稳定运行、看起来像 phone home、架构上能继续长大的嵌入式系统。
