import { FATE_DROPPED, FATE_EXPANDED, FATE_OPEN, FATE_SOLUTION } from './exploration.ts'
import { childCountOf, type SearchTree } from './search-tree.ts'

export interface DepthRow {
  depth: number
  states: number
  expanded: number
  open: number
  dropped: number
  solution: number
  children: number
  averageBranching: number
  averageHeuristic: number
  minimumHeuristic: number
}

export interface HeuristicBin {
  from: number
  to: number
  expanded: number
  open: number
  dropped: number
  solution: number
}

export interface ProgressPoint {
  expansion: number
  bestHeuristic: number
  cumulativeMicroseconds: number
  depth: number
}

const emptyRow = (depth: number): DepthRow => ({
  depth,
  states: 0,
  expanded: 0,
  open: 0,
  dropped: 0,
  solution: 0,
  children: 0,
  averageBranching: 0,
  averageHeuristic: 0,
  minimumHeuristic: Infinity,
})

// One row per tree depth: how many states live there, what became of them, and how bushy they are.
export function depthProfile(tree: SearchTree): DepthRow[] {
  const { fate, heuristic } = tree.exploration
  const rows = Array.from({ length: tree.maxDepth + 1 }, (_, depth) => emptyRow(depth))
  const heuristicSums = new Float64Array(rows.length)

  for (let index = 0; index < tree.nodeCount; index++) {
    const row = rows[tree.depth[index]!]!
    const value = heuristic[index]!
    const nodeFate = fate[index]!

    row.states++
    heuristicSums[row.depth]! += value
    row.minimumHeuristic = Math.min(row.minimumHeuristic, value)

    if (nodeFate === FATE_EXPANDED) {
      row.expanded++
      row.children += childCountOf(tree, index)
    } else if (nodeFate === FATE_SOLUTION) row.solution++
    else if (nodeFate === FATE_DROPPED) row.dropped++
    else if (nodeFate === FATE_OPEN) row.open++
  }

  for (const row of rows) {
    row.averageBranching = row.expanded > 0 ? row.children / row.expanded : 0
    row.averageHeuristic = row.states > 0 ? heuristicSums[row.depth]! / row.states : 0
    if (row.states === 0) row.minimumHeuristic = 0
  }

  return rows
}

export function heuristicHistogram(tree: SearchTree, maxBins: number): HeuristicBin[] {
  const { fate, heuristic } = tree.exploration
  let low = Infinity
  let high = -Infinity

  // the start state never gets a heuristic (stored as 0), so it would only skew the distribution
  for (let index = 1; index < tree.nodeCount; index++) {
    const value = heuristic[index]!
    if (value < low) low = value
    if (value > high) high = value
  }
  if (low > high) return []

  const span = high - low + 1
  const width = Math.max(1, Math.ceil(span / maxBins))
  const bins: HeuristicBin[] = Array.from({ length: Math.ceil(span / width) }, (_, bin) => ({
    from: low + bin * width,
    to: Math.min(high, low + (bin + 1) * width - 1),
    expanded: 0,
    open: 0,
    dropped: 0,
    solution: 0,
  }))

  for (let index = 1; index < tree.nodeCount; index++) {
    const bin = bins[Math.floor((heuristic[index]! - low) / width)]!
    const nodeFate = fate[index]!

    if (nodeFate === FATE_EXPANDED) bin.expanded++
    else if (nodeFate === FATE_SOLUTION) bin.solution++
    else if (nodeFate === FATE_DROPPED) bin.dropped++
    else bin.open++
  }

  return bins
}

// Replays the run in pop order: the best heuristic seen so far and the solver time spent so far,
// thinned to about maxPoints so the chart stays cheap on million-state trees.
export function progressCurve(tree: SearchTree, maxPoints: number): ProgressPoint[] {
  const { expansionOrder, heuristic, stepMicroseconds } = tree.exploration
  let expansions = 0
  for (let index = 0; index < tree.nodeCount; index++) {
    if (expansionOrder[index]! > 0) expansions++
  }
  if (expansions === 0) return []

  const nodeAtPop = new Int32Array(expansions + 1).fill(-1)
  for (let index = 0; index < tree.nodeCount; index++) {
    const pop = expansionOrder[index]!
    if (pop > 0 && pop <= expansions) nodeAtPop[pop] = index
  }

  const stride = Math.max(1, Math.floor(expansions / maxPoints))
  const points: ProgressPoint[] = []
  let best = Infinity
  let elapsed = 0

  for (let pop = 1; pop <= expansions; pop++) {
    const index = nodeAtPop[pop]!
    if (index < 0) continue

    if (index !== 0) best = Math.min(best, heuristic[index]!)
    elapsed += stepMicroseconds[index]!

    if (best !== Infinity && (pop % stride === 0 || pop === 1 || pop === expansions)) {
      points.push({
        expansion: pop,
        bestHeuristic: best,
        cumulativeMicroseconds: elapsed,
        depth: tree.depth[index]!,
      })
    }
  }

  return points
}

const DEPTH_CSV_HEADER = [
  'depth',
  'states',
  'expanded',
  'queued',
  'dropped',
  'solution',
  'children_of_expanded',
  'avg_branching',
  'avg_heuristic',
  'min_heuristic',
]

export const depthProfileCsv = (profile: readonly DepthRow[]) =>
  toCsv(
    DEPTH_CSV_HEADER,
    profile.map((r) => [
      r.depth,
      r.states,
      r.expanded,
      r.open,
      r.dropped,
      r.solution,
      r.children,
      r.averageBranching.toFixed(4),
      r.averageHeuristic.toFixed(4),
      r.minimumHeuristic,
    ]),
  )

export const progressCsv = (progress: readonly ProgressPoint[]) =>
  toCsv(
    ['expansion', 'best_heuristic', 'cumulative_microseconds', 'depth'],
    progress.map((p) => [p.expansion, p.bestHeuristic, p.cumulativeMicroseconds, p.depth]),
  )

export function toCsv(header: readonly string[], rows: readonly (readonly (string | number)[])[]) {
  const escape = (cell: string | number) => {
    const text = String(cell)
    return /[",\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text
  }

  return [header, ...rows].map((row) => row.map(escape).join(',')).join('\n') + '\n'
}
