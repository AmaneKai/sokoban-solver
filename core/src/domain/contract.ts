// Status and summary types shared by the engine services and every user interface.

export type StopReason = '' | 'expansions' | 'time' | 'size' | 'stopped' | 'pool'

export interface ExploreSummary {
  threadId: number
  /** Backward (pull) search: the tree starts at the solved board and each move is a pull. */
  backward?: boolean
  width: number
  height: number
  expansionCount: number
  stopReason: StopReason
  isSolved: boolean
  nodeCount: number
  detailCount: number
  reasonTotals: readonly number[]
  totalNanoseconds: number
  phaseNanoseconds: readonly number[]
  switchPenalty: number
}

export type JobState = 'idle' | 'running' | 'done' | 'unsolved' | 'timeout' | 'cancelled' | 'failed'

export interface SolveStatus {
  state: JobState
  solution: string
  error: string
  elapsedSeconds: number
  solverMilliseconds: number | null
  winnerThreadId: number | null
  winnerNodeCount: number | null
}

export interface ExploreStatus {
  state: JobState
  error: string
  elapsedSeconds: number
  summary: ExploreSummary | null
}

/** One solver found in `solvers/`. Only a solver with a tracer can be explored. */
export interface SolverInfo {
  id: string
  name: string
  built: boolean
  /** True when the solver has a tracer, so the Explorer can open its search tree. */
  explorable: boolean
  /** True when the solver accepts the thread selector. */
  threads: boolean
}

export interface MapList {
  maps: string[]
  built: boolean
}

export interface MapText {
  name: string
  text: string
}

export interface BuildResult {
  ok: boolean
  error?: string
}
