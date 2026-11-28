export interface ProtocolFixtureItem {
  contract: string
  kind: 'valid' | 'invalid'
  schema: string | null
  filename: string
  content: unknown
}

let inFlightLoad: Promise<ProtocolFixtureItem[]> | null = null

export function useProtocolFixtures() {
  const { locale } = useSiteLocale()
  const { withBase } = useSiteAssetUrl()

  const items = useState<ProtocolFixtureItem[]>('rodakos-protocol-fixtures', () => [])
  const loaded = useState<boolean>('rodakos-protocol-fixtures-loaded', () => false)
  const loadError = useState<string | null>('rodakos-protocol-fixtures-error', () => null)

  const isEn = computed(() => locale.value === 'en')

  function t(zh: string, en: string): string {
    return isEn.value ? en : zh
  }

  async function ensureLoaded(): Promise<ProtocolFixtureItem[]> {
    if (loaded.value) return items.value
    if (!import.meta.client) return items.value
    if (!inFlightLoad) {
      inFlightLoad = fetch(withBase('protocols/data/fixtures.json'))
        .then(async (res) => {
          if (!res.ok) throw new Error(`fixtures.json: HTTP ${res.status}`)
          const data = (await res.json()) as ProtocolFixtureItem[]
          items.value = data
          loaded.value = true
          loadError.value = null
          return data
        })
        .catch((err) => {
          loadError.value = String(err)
          return []
        })
        .finally(() => {
          inFlightLoad = null
        })
    }
    return inFlightLoad
  }

  async function getFixture<T = unknown>(filename: string): Promise<T | null> {
    const list = await ensureLoaded()
    const match = list.find((item) => item.filename === filename)
    return (match?.content as T) ?? null
  }

  return {
    items,
    loaded,
    loadError,
    isEn,
    t,
    ensureLoaded,
    getFixture
  }
}
