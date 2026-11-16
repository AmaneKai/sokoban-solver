import {
  FATE_DROPPED,
  FATE_EXPANDED,
  FATE_OPEN,
  PRUNE_REASONS,
  ROOT_MOVE,
  unpackPush,
  type Cell,
  type PruneReason,
  type PrunedPush,
  type Push,
} from './exploration.ts'
import type { ExploreSummary } from './contract.ts'
import { formatCount, formatDuration, formatPercent } from '../format.ts'
import { positionKey, type Puzzle, type Session } from './puzzle.ts'
import { childCountOf, routeTo, type SearchTree } from './search-tree.ts'

interface TreeStats {
  generatedCount: number
  poppedCount: number
  deadEndCount: number
  consideredPushCount: number
  rejectedByReason: ReadonlyMap<PruneReason, number>
  droppedByReason: ReadonlyMap<PruneReason, number>
}

export interface SwitchStats {
  switchedCount: number
  samePushCount: number
}

export interface StateDetails {
  index: number
  depth: number
  push: Push | null
  pushCount: number
  heuristic: number
  fate: 'open' | 'expanded' | 'solution' | 'dropped'
  expansionOrder: number
  stepMicroseconds: number
  dropReason: PruneReason | null
  rejectedByReason: ReadonlyMap<PruneReason, number>
  switched: boolean
  switchesSoFar: number
}

export const PRUNE_LABELS: Record<PruneReason, string> = {
  none: 'None',
  outsideCorral: 'Outside the PI-corral',
  deadSquare: 'Dead square',
  twoByTwo: '2×2 block',
  freeze: 'Frozen box',
  duplicate: 'Already seen (duplicate)',
  deadHeuristic: 'Heuristic says unsolvable',
  piCorralDeadlock: 'PI-corral deadlock',
  hallInfeasible: 'No goal left for a box (Hall)',
}

// Labels of the solver phases, in the order of ExploreSummary.phaseNanoseconds.
export const PHASE_LABELS: readonly string[] = [
  'Node checks',
  ...PRUNE_REASONS.slice(1).map((reason) => `Reject: ${PRUNE_LABELS[reason]}`),
  'Queue state',
  'Goal check',
  'Player reach fill',
  'PI-corral search',
  'Hall feasibility',
  'Push validation',
  'State assembly',
  'Player land fill',
  'Box copy',
  'Hash insert (dup)',
  'Heuristic calc',
]

const FATE_NAMES = ['open', 'expanded', 'solution', 'dropped'] as const
const REJECTED_REASON_COUNT = 8
const FIRST_NODE_LEVEL_REASON = 7

const statsCache = new WeakMap<SearchTree, TreeStats>()

export function computeTreeStats(tree: SearchTree): TreeStats {
  const cached = statsCache.get(tree)
  if (cached) return cached

  const { exploration } = tree
  const totals = exploration.summary.reasonTotals
  let deadEndCount = 0

  for (let index = 0; index < tree.nodeCount; index++) {
    const fate = exploration.fate[index]!
    if (fate === FATE_EXPANDED && childCountOf(tree, index) === 0) deadEndCount++
  }

  const totalOf = (reason: PruneReason) => totals[PRUNE_REASONS.indexOf(reason)] ?? 0
  const pushReasons = PRUNE_REASONS.slice(1, FIRST_NODE_LEVEL_REASON)
  const nodeReasons = PRUNE_REASONS.slice(FIRST_NODE_LEVEL_REASON)
  const rejectedByReason = new Map(pushReasons.map((reason) => [reason, totalOf(reason)]))
  const generatedCount = Math.max(0, tree.nodeCount - 1)

  let rejectedCount = 0
  for (const count of rejectedByReason.values()) rejectedCount += count

  const stats: TreeStats = {
    generatedCount,
    poppedCount: exploration.summary.expansionCount,
    deadEndCount,
    consideredPushCount: generatedCount + rejectedCount,
    rejectedByReason,
    droppedByReason: new Map(nodeReasons.map((reason) => [reason, totalOf(reason)])),
  }
  statsCache.set(tree, stats)
  return stats
}

const switchCache = new WeakMap<SearchTree, { switched: Uint8Array; total: Uint16Array }>()

// A push "switches" when it moves a different box than the push that led here;
// the solver charges switch_penalty once per such push (and it accumulates down the path).
function switchesOf(tree: SearchTree) {
  const cached = switchCache.get(tree)
  if (cached) return cached

  const { parentIndex, move } = tree.exploration
  const switched = new Uint8Array(tree.nodeCount)
  const total = new Uint16Array(tree.nodeCount)

  for (let index = 1; index < tree.nodeCount; index++) {
    const parent = parentIndex[index]!
    const parentMove = move[parent]!
    const isSwitch = parentMove !== ROOT_MOVE && move[index]! >>> 16 !== (parentMove & 0xffff)
    switched[index] = isSwitch ? 1 : 0
    total[index] = total[parent]! + switched[index]!
  }

  const result = { switched, total }
  switchCache.set(tree, result)
  return result
}

export function computeSwitchStats(tree: SearchTree): SwitchStats {
  const { switched } = switchesOf(tree)
  let switchedCount = 0
  for (let index = 1; index < tree.nodeCount; index++) switchedCount += switched[index]!

  return { switchedCount, samePushCount: Math.max(0, tree.nodeCount - 1) - switchedCount }
}

export function stateDetails(tree: SearchTree, index: number): StateDetails {
  const { exploration } = tree
  const fate = exploration.fate[index]! ?? FATE_OPEN
  const rejectedByReason = new Map<PruneReason, number>()

  for (let reason = 0; reason < REJECTED_REASON_COUNT; reason++) {
    const count = exploration.rejectedCounts[index * REJECTED_REASON_COUNT + reason]!
    const name = PRUNE_REASONS[reason + 1]
    if (count > 0 && name) rejectedByReason.set(name, count)
  }

  return {
    index,
    depth: tree.depth[index]!,
    push: unpackPush(exploration.move[index]!),
    pushCount: exploration.pushCount[index]!,
    heuristic: exploration.heuristic[index]!,
    fate: FATE_NAMES[fate] ?? 'open',
    expansionOrder: exploration.expansionOrder[index]!,
    stepMicroseconds: exploration.stepMicroseconds[index]!,
    dropReason:
      fate === FATE_DROPPED ? (PRUNE_REASONS[exploration.dropReason[index]!] ?? null) : null,
    rejectedByReason,
    switched: switchesOf(tree).switched[index]! === 1,
    switchesSoFar: switchesOf(tree).total[index]!,
  }
}

export function rejectedPushesOf(tree: SearchTree, index: number): PrunedPush[] {
  const { detailParent, detailMove, detailReason } = tree.exploration
  const parentAt = (position: number) => detailParent[tree.detailOrder[position]!]!

  let low = 0
  let high = tree.detailOrder.length

  while (low < high) {
    const middle = (low + high) >> 1
    if (parentAt(middle) < index) low = middle + 1
    else high = middle
  }

  const pushes: PrunedPush[] = []
  for (let pos = low; pos < tree.detailOrder.length && parentAt(pos) === index; pos++) {
    const detailIndex = tree.detailOrder[pos]!
    const push = unpackPush(detailMove[detailIndex]!)
    const reason = PRUNE_REASONS[detailReason[detailIndex]!]
    if (push && reason) pushes.push({ push, reason })
  }

  return pushes
}

// A push leaves the player behind the box; a pull leaves them ahead of it, where they stood.
const applyMove = (
  boxes: ReadonlySet<string>,
  { from, to }: Push,
  backward: boolean,
): [Set<string>, Cell] => {
  const next = new Set(boxes)
  next.delete(positionKey(from))
  next.add(positionKey(to))

  const sign = backward ? -1 : 1
  const rowStep = Math.sign(to[0] - from[0])
  const columnStep = Math.sign(to[1] - from[1])

  return [next, [to[0] - sign * rowStep, to[1] - sign * columnStep]]
}

export function sessionAtNode(puzzle: Puzzle, tree: SearchTree, index: number): Session {
  const backward = tree.exploration.summary.backward === true
  // A backward search starts from the solved board: every box on a goal.
  const root: Session = backward
    ? { player: puzzle.start.player, boxes: puzzle.goals, history: [] }
    : puzzle.start

  return routeTo(tree, index).reduce<Session>((session, step) => {
    const push = unpackPush(tree.exploration.move[step]!)
    if (!push) return session

    const [boxes, player] = applyMove(session.boxes, push, backward)
    return { player, boxes, history: [] }
  }, root)
}

export function slowestSteps(tree: SearchTree, count: number): number[] {
  const { stepMicroseconds, expansionOrder } = tree.exploration
  const durationOf = (index: number) => stepMicroseconds[index]!
  const slowest: number[] = []

  for (let index = 0; index < tree.nodeCount; index++) {
    if (expansionOrder[index]! === 0) continue

    const duration = durationOf(index)
    if (slowest.length === count && duration <= durationOf(slowest[count - 1]!)) continue

    let pos = slowest.length
    while (pos > 0 && durationOf(slowest[pos - 1]!) < duration) pos--

    slowest.splice(pos, 0, index)
    if (slowest.length > count) slowest.pop()
  }

  return slowest
}

export interface MetricTile {
  label: string
  value: string
  hint: string
}

/** The headline numbers of a finished run, in display order. */
export function keyMetrics(tree: SearchTree, summary: ExploreSummary): MetricTile[] {
  const stats = computeTreeStats(tree)
  const switches = computeSwitchStats(tree)
  const rejected = stats.consideredPushCount - stats.generatedCount
  const seconds = summary.totalNanoseconds / 1e9

  return [
    {
      label: 'Outcome',
      value: summary.isSolved ? 'Solved' : summary.stopReason ? 'Stopped' : 'No solution',
      hint: summary.isSolved
        ? `${formatCount(Math.max(0, tree.solutionPath.length - 1))} pushes deep`
        : summary.stopReason || 'search space exhausted',
    },
    {
      label: 'Search time',
      value: formatDuration(summary.totalNanoseconds),
      hint: `thread ${summary.threadId}`,
    },
    {
      label: 'Throughput',
      value: seconds > 0 ? `${formatCount(Math.round(stats.poppedCount / seconds))}/s` : '-',
      hint: 'expansions per second',
    },
    {
      label: 'States expanded',
      value: formatCount(stats.poppedCount),
      hint: `of ${formatCount(tree.nodeCount)} generated`,
    },
    { label: 'Max depth', value: formatCount(tree.maxDepth), hint: 'pushes from the start' },
    {
      label: 'Avg branching',
      value: (stats.generatedCount / Math.max(stats.poppedCount, 1)).toFixed(2),
      hint: 'new states per expansion',
    },
    {
      label: 'Pruned pushes',
      value: formatPercent(rejected, stats.consideredPushCount),
      hint: `${formatCount(rejected)} of ${formatCount(stats.consideredPushCount)} tried`,
    },
    {
      label: 'Dead ends',
      value: formatCount(stats.deadEndCount),
      hint: `${formatPercent(stats.deadEndCount, stats.poppedCount)} of expanded`,
    },
    {
      label: 'Box switches',
      value: formatPercent(switches.switchedCount, stats.generatedCount),
      hint: summary.switchPenalty > 0 ? `penalty ${summary.switchPenalty} applied` : 'penalty off',
    },
  ]
}
