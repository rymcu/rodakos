# RodakOS 官网落地页、在线文档与协议实验室（`site/`）

基于 **Nuxt 4 + Nuxt UI 4 + Nuxt Content v3 + Tailwind CSS 4** 构建的静态生成（SSG）官网站点，同时在 `/protocols/` 挂载交互式协议实验室。

## 目录隔离说明

`site/` 拥有独立的 `pnpm-workspace.yaml` 与 `pnpm-lock.yaml`，与根目录 ESP-IDF 固件工程完全隔离，并通过 Node 原生 `node:sqlite` 运行 `@nuxt/content`。关闭了外部字体 CDN，支持离线构建。

## 常用命令

```powershell
cd site
pnpm install           # 安装站点依赖
pnpm dev               # 启动本地开发服务器
pnpm typecheck         # TypeScript 类型检查
pnpm test:protocols    # 运行协议实验室逻辑单元测试
pnpm generate          # 生成纯静态站点（输出至 site/.output/public）
pnpm preview           # 本地预览静态构建产物（http://127.0.0.1:4174）
```

如需构建到 GitHub Pages 仓库子路径（例如 `/rodakos/`），设置环境变量后执行生成：

```powershell
$env:NUXT_APP_BASE_URL="/rodakos/"
pnpm generate
```

## 内容与结构

- `app/config/links.ts`：集中管理 GitHub 仓库、Rodak 发布页、BigSmart-Open 硬件仓与站内路由链接
- `app/pages/index.vue`：基于 Nuxt UI 4 官方 Landing 组件（`UPageHero` / `UPageSection` / `UPageCard` / `UTabs` / `UAccordion` / `UPageCTA`）构建的落地页
- `app/pages/docs/[...slug].vue`：在线文档站（含侧栏导航、全文搜索与目录 TOC）
- `content/docs/`：快速上手、架构与刷机、硬件与 3D 外壳、协议导读的 Markdown 文档
- `public/screenshots/`：由真实 LVGL 9.3 主机显存导出的 320×240 界面渲染图（3× 锐利放大 WebP）
- `public/protocols/data/`：协议实验室（`app/pages/protocols/`：绑定状态机、Envelope、RAV1 拆帧、MQTT 重放、OTA 浏览器验签）使用的静态数据，由 `tools/build_site_data.py` 从 `contracts/` 生成
