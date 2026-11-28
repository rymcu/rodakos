import type { SiteLocale } from '~/types/landing'

const LOCALE_STORAGE_KEY = 'rodakos-site-locale'

export function useSiteLocale() {
  const locale = useState<SiteLocale>('rodakos-site-locale', () => 'zh')

  function setLocale(next: SiteLocale): void {
    locale.value = next
    if (import.meta.client) {
      try {
        window.localStorage.setItem(LOCALE_STORAGE_KEY, next)
      } catch {
        // 忽略受限存储环境
      }
    }
  }

  function toggleLocale(): void {
    setLocale(locale.value === 'zh' ? 'en' : 'zh')
  }

  function hydrateLocale(): void {
    if (!import.meta.client) return
    try {
      const saved = window.localStorage.getItem(LOCALE_STORAGE_KEY)
      if (saved === 'zh' || saved === 'en') {
        locale.value = saved
      }
    } catch {
      // 忽略受限存储环境
    }
  }

  return {
    locale: readonly(locale),
    setLocale,
    toggleLocale,
    hydrateLocale
  }
}
