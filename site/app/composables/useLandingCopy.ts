import { LANDING_COPY_EN } from '~/data/landing-en'
import { LANDING_COPY_ZH } from '~/data/landing-zh'
import type { LandingCopy } from '~/types/landing'

export function useLandingCopy() {
  const { locale, setLocale, toggleLocale } = useSiteLocale()

  const copy = computed<LandingCopy>(() =>
    locale.value === 'en' ? LANDING_COPY_EN : LANDING_COPY_ZH
  )

  return {
    locale,
    copy,
    setLocale,
    toggleLocale
  }
}
