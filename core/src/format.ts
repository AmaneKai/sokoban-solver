// Text helpers and the status notice shared by every user interface.

export const formatCount = (count: number) => count.toLocaleString('en-US')

export const formatPercent = (part: number, whole: number) =>
  whole === 0 ? '0%' : `${((part / whole) * 100).toFixed(1)}%`

export function formatDuration(nanoseconds: number): string {
  if (nanoseconds < 1_000) return `${nanoseconds} ns`
  if (nanoseconds < 1_000_000) return `${(nanoseconds / 1_000).toFixed(1)} µs`
  if (nanoseconds < 1_000_000_000) return `${(nanoseconds / 1_000_000).toFixed(2)} ms`

  return `${(nanoseconds / 1_000_000_000).toFixed(2)} s`
}

export interface Notice {
  text: string
  tone: '' | 'ok' | 'bad'
}

export const IDLE: Notice = { text: 'Idle.', tone: '' }
