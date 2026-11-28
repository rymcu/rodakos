export function useClipboardCopy() {
  const copiedKey = shallowRef<string | null>(null)
  let resetTimer: ReturnType<typeof setTimeout> | null = null

  async function copyText(key: string, text: string): Promise<boolean> {
    if (!import.meta.client) return false

    try {
      await navigator.clipboard.writeText(text)
    } catch {
      try {
        const textarea = document.createElement('textarea')
        textarea.value = text
        textarea.setAttribute('readonly', '')
        textarea.style.position = 'fixed'
        textarea.style.opacity = '0'
        document.body.appendChild(textarea)
        textarea.select()
        document.execCommand('copy')
        document.body.removeChild(textarea)
      } catch {
        return false
      }
    }

    copiedKey.value = key
    if (resetTimer) {
      clearTimeout(resetTimer)
    }
    resetTimer = setTimeout(() => {
      copiedKey.value = null
      resetTimer = null
    }, 2000)
    return true
  }

  onBeforeUnmount(() => {
    if (resetTimer) {
      clearTimeout(resetTimer)
      resetTimer = null
    }
  })

  return {
    copiedKey: readonly(copiedKey),
    copyText
  }
}
