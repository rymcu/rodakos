export default defineNuxtConfig({
  compatibilityDate: '2025-07-15',
  devtools: { enabled: false },

  // @nuxt/content 必须位于 @nuxt/ui 之后，以确保 Prose 组件正常注册
  modules: ['@nuxt/ui', '@nuxt/content'],

  css: ['~/assets/css/main.css'],

  ui: {
    // 关闭外部字体 CDN 拉取，保持本地优先与离线可构建
    fonts: false
  },

  colorMode: {
    preference: 'dark',
    fallback: 'dark'
  },

  icon: {
    serverBundle: {
      collections: ['lucide', 'simple-icons']
    }
  },

  content: {
    experimental: {
      sqliteConnector: 'native',
      nativeSqlite: true
    },
    build: {
      markdown: {
        highlight: {
          theme: {
            default: 'github-light',
            dark: 'github-dark'
          },
          langs: [
            'bash',
            'sh',
            'powershell',
            'python',
            'ini',
            'c',
            'cpp',
            'cmake',
            'csv',
            'json',
            'ts',
            'typescript',
            'yaml',
            'markdown',
            'vue',
            'mermaid'
          ]
        }
      }
    }
  },

  app: {
    head: {
      title: 'RodakOS — 面向 ESP32-S3 的开源本地优先掌上 “Phone OS”',
      meta: [
        { charset: 'utf-8' },
        { name: 'viewport', content: 'width=device-width, initial-scale=1' },
        {
          name: 'description',
          content:
            '基于 ESP-IDF 6.0.2 与 LVGL 9.3 打造的开源 ESP32-S3 终端固件。在 320×240 触控屏上提供 16 款原生应用、离线 MultiNet 唤醒与逐帧语音字幕、免 USB 的 LAN SRP6a 配网及不可变 Recovery 签名 OTA。'
        },
        { property: 'og:type', content: 'website' },
        { property: 'og:title', content: 'RodakOS — 面向 ESP32-S3 的开源本地优先掌上 “Phone OS”' },
        {
          property: 'og:description',
          content:
            '开源 ESP32-S3 触控终端固件与开放 AIoT 协议，原生适配 RYMCU BigSmart-Open 硬件并协同 Rodak 本地工作台。'
        },
        { name: 'theme-color', content: '#070c11' }
      ]
    }
  },

  nitro: {
    prerender: {
      crawlLinks: true,
      concurrency: 1,
      routes: ['/', '/docs']
    }
  },

  typescript: {
    strict: true
  }
})
