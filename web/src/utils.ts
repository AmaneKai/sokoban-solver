// Browser-only helpers. Pure helpers live in @sokobot/core.

export const isTyping = (event: Event) =>
  event.target instanceof HTMLElement && event.target.matches('input, select')

export const normalizeKey = (event: KeyboardEvent) =>
  event.key.length === 1 ? event.key.toLowerCase() : event.key

export function download(filename: string, text: string, type: string) {
  const url = URL.createObjectURL(new Blob([text], { type }))
  const link = Object.assign(document.createElement('a'), { href: url, download: filename })
  link.click()
  URL.revokeObjectURL(url)
}
