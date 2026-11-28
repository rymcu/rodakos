export function useSiteAssetUrl() {
  const runtimeConfig = useRuntimeConfig()

  function withBase(relativePath: string): string {
    const base = runtimeConfig.app.baseURL.endsWith('/')
      ? runtimeConfig.app.baseURL
      : `${runtimeConfig.app.baseURL}/`
    const normalized = relativePath.startsWith('/') ? relativePath.slice(1) : relativePath
    return `${base}${normalized}`
  }

  return {
    withBase
  }
}
