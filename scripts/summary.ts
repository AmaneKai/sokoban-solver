// Static summary of a finished telemetry run. Plain ANSI text, no dependencies.
import {
  computeTreeStats,
  depthProfile,
  formatCount,
  formatDuration,
  PHASE_LABELS,
  PRUNE_LABELS,
  progressCurve,
  type SearchTree,
} from '@sokobot/core'

const STOP_TEXT: Record<string, string> = {
  '': 'finished',
  expansions: 'expansion limit',
  time: 'time limit',
  size: '10M node cap',
  stopped: 'stopped',
  pool: 'state pool full (unsolved)',
}

const c = (code: number, text: string) => `\x1b[${code}m${text}\x1b[0m`
const BLOCKS = '▁▂▃▄▅▆▇█'
const MAX_WIDTH = 100
const MAX_DEPTH_BARS = 16
const TOP_PHASES = 8

function sparkline(values: number[], width: number) {
  if (values.length === 0) return ''
  const cols = Math.min(width, values.length)
  const picked = Array.from({ length: cols }, (_, i) => values[Math.floor((i * values.length) / cols)]!)
  const lo = Math.min(...picked)
  const span = Math.max(...picked) - lo || 1
  return picked.map((v) => BLOCKS[Math.round(((v - lo) / span) * (BLOCKS.length - 1))]).join('')
}

interface BarRow {
  label: string
  value: number
  text: string
}

function bars(rows: BarRow[], width: number, color: number) {
  const top = Math.max(...rows.map((r) => r.value), 1)
  const labelWidth = Math.max(...rows.map((r) => r.label.length))
  return rows.map((r) => {
    const filled = Math.max(r.value > 0 ? 1 : 0, Math.round((r.value / top) * width))
    return `  ${r.label.padEnd(labelWidth)} ${c(color, '█'.repeat(filled))} ${c(2, r.text)}`
  })
}

const heading = (text: string) => `\n${c(1, c(36, text))}`

export function renderSummary(mapName: string, tree: SearchTree, columns = 80): string {
  const { summary } = tree.exploration
  const width = Math.min(columns, MAX_WIDTH)
  const barWidth = Math.max(10, width - 44)
  const out: string[] = []

  const status = summary.isSolved ? c(32, 'solved') : c(33, 'unsolved')
  out.push(`${c(1, mapName)}  ${c(2, `thread ${summary.threadId} · ${summary.width}x${summary.height}`)}`)
  out.push(
    `${status} · ${STOP_TEXT[summary.stopReason] ?? summary.stopReason} · ` +
      `${formatCount(summary.expansionCount)} expansions · ${formatCount(summary.nodeCount)} states · ` +
      formatDuration(summary.totalNanoseconds),
  )

  const progress = progressCurve(tree, width)
  if (progress.length > 0) {
    const last = progress[progress.length - 1]!
    out.push(heading('Best heuristic over the search (lower is closer to the goal)'))
    out.push(`  ${c(35, sparkline(progress.map((p) => p.bestHeuristic), width - 4))}`)
    out.push(c(2, `  ${progress[0]!.bestHeuristic} to ${last.bestHeuristic} over ${formatCount(last.expansion)} expansions`))

    out.push(heading('Depth reached over the search'))
    out.push(`  ${c(34, sparkline(progress.map((p) => p.depth), width - 4))}`)
    out.push(c(2, `  max depth ${tree.maxDepth}`))
  }

  // Bucket depths so deep trees still fit on one screen.
  const profile = depthProfile(tree)
  const bucketSize = Math.max(1, Math.ceil(profile.length / MAX_DEPTH_BARS))
  const buckets: BarRow[] = []
  for (let i = 0; i < profile.length; i += bucketSize) {
    const slice = profile.slice(i, i + bucketSize)
    const states = slice.reduce((sum, r) => sum + r.states, 0)
    const expanded = slice.reduce((sum, r) => sum + r.expanded, 0)
    const children = slice.reduce((sum, r) => sum + r.children, 0)
    const end = slice[slice.length - 1]!.depth
    buckets.push({
      label: bucketSize === 1 ? `d${i}` : `d${i}-${end}`,
      value: states,
      text: `${formatCount(states)} states, branching ${(expanded ? children / expanded : 0).toFixed(1)}`,
    })
  }
  out.push(heading('States per depth'))
  out.push(...bars(buckets, barWidth, 34))

  const stats = computeTreeStats(tree)
  const outcomes = [
    { label: 'Became state', value: stats.generatedCount, text: '' },
    ...[...stats.rejectedByReason].map(([reason, value]) => ({ label: PRUNE_LABELS[reason], value, text: '' })),
  ]
    .filter((r) => r.value > 0)
    .sort((a, b) => b.value - a.value)
    .map((r) => ({ ...r, text: formatCount(r.value) }))
  out.push(heading('Where pushes went'))
  out.push(...bars(outcomes, barWidth, 33))

  const phases = summary.phaseNanoseconds
    .map((ns, i) => ({ label: PHASE_LABELS[i] ?? `Phase ${i}`, value: ns, text: formatDuration(ns) }))
    .filter((r) => r.value > 0)
    .sort((a, b) => b.value - a.value)
    .slice(0, TOP_PHASES)
  if (phases.length > 0) {
    out.push(heading(`Where the time went (top ${TOP_PHASES} phases)`))
    out.push(...bars(phases, barWidth, 31))
  }

  return out.join('\n') + '\n'
}
