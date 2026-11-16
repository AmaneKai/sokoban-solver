import type { EngineService } from '../client/engine-service.ts'
import type { JobState, SolveStatus, SolverInfo } from '../domain/contract.ts'
import { IDLE, type Notice } from '../format.ts'
import {
  movePlayer,
  parsePuzzle,
  undoMove,
  type Direction,
  type Puzzle,
  type Session,
} from '../domain/puzzle.ts'
import { Observable, pollJob } from './observable.ts'

const SLOWEST_PLAYBACK_DELAY_MS = 1000
export const FASTEST_SPEED_SETTING = 10
export const SLOWEST_SPEED_SETTING = 1

/** Delay between two playback steps for a speed setting from 1 to 10. */
export function playbackDelayMs(speed: number): number {
  return speed >= FASTEST_SPEED_SETTING
    ? 0
    : Math.round(SLOWEST_PLAYBACK_DELAY_MS * 0.5 ** (speed * 0.8))
}

export interface SolverState {
  maps: string[]
  solvers: SolverInfo[]
  /** Id of the solver that runs Solve. */
  solverId: string
  mapName: string
  puzzle: Puzzle | null
  session: Session | null
  solution: string
  played: number
  playing: boolean
  speed: number
  timeoutSeconds: number
  /** Which thread config solves: -1 = the whole portfolio (auto). */
  thread: number
  solving: boolean
  winnerThread: number | null
  notice: Notice
}

export interface SolverControllerOptions {
  /** Called after a map loads. A user interface can use it to remember the choice. */
  onMapSelected?: (name: string) => void
  /** Called after the user picks a solver. */
  onSolverSelected?: (id: string) => void
}

export function describeSolveOutcome(status: SolveStatus): Notice {
  const seconds = (status.solverMilliseconds ?? status.elapsedSeconds * 1000) / 1000
  const { winnerThreadId, winnerNodeCount } = status
  const winner =
    winnerThreadId !== null && winnerNodeCount !== null
      ? ` · thread ${winnerThreadId} won · ${winnerNodeCount.toLocaleString('en-US')} nodes`
      : ''

  const outcomes: Record<JobState, Notice> = {
    idle: IDLE,
    running: { text: `Solving... ${seconds.toFixed(1)}s`, tone: '' },
    done: { text: `Solver time: ${seconds.toFixed(3)}s${winner}`, tone: 'ok' },
    timeout: { text: `Timed out after ${seconds.toFixed(2)}s.`, tone: 'bad' },
    unsolved: { text: `No solution found (${seconds.toFixed(2)}s).`, tone: 'bad' },
    failed: { text: `Solver failed: ${status.error}`, tone: 'bad' },
    cancelled: { text: 'Cancelled.', tone: '' },
  }

  return outcomes[status.state]
}

/** Owns the state of the solver view: the puzzle, the player session, and the bot playback. */
export class SolverController extends Observable<SolverState> {
  readonly #engine: EngineService
  readonly #options: SolverControllerOptions
  #stopPolling: (() => void) | null = null
  #playbackTimer: ReturnType<typeof setTimeout> | null = null

  constructor(engine: EngineService, options: SolverControllerOptions = {}) {
    super({
      maps: [],
      solvers: [],
      solverId: '',
      mapName: '',
      puzzle: null,
      session: null,
      solution: '',
      played: 0,
      playing: false,
      speed: 7,
      timeoutSeconds: 60,
      thread: -1,
      solving: false,
      winnerThread: null,
      notice: IDLE,
    })
    this.#engine = engine
    this.#options = options
  }

  /** Schedules the next playback step whenever the playback state changes. */
  protected override onUpdate(patch: Partial<SolverState>): void {
    if (!('playing' in patch || 'played' in patch || 'speed' in patch || 'solution' in patch))
      return

    if (this.#playbackTimer !== null) clearTimeout(this.#playbackTimer)
    this.#playbackTimer = null

    const { playing, puzzle, played, solution, speed } = this.state
    if (!playing || !puzzle) return
    if (played >= solution.length) {
      this.update({ playing: false })
      return
    }

    this.#playbackTimer = setTimeout(() => {
      this.#playbackTimer = null
      this.step()
    }, playbackDelayMs(speed))
  }

  setNotice(notice: Notice) {
    this.update({ notice })
  }

  pause() {
    this.update({ playing: false })
  }

  setSpeed(speed: number) {
    this.update({ speed: Math.min(FASTEST_SPEED_SETTING, Math.max(SLOWEST_SPEED_SETTING, speed)) })
  }

  setSolver(id: string) {
    if (!this.state.solvers.some((solver) => solver.id === id)) return

    this.update({ solverId: id })
    this.#options.onSolverSelected?.(id)
  }

  setThread(thread: number) {
    this.update({ thread: Math.trunc(thread) >= 0 ? Math.trunc(thread) : -1 })
  }

  setTimeoutSeconds(seconds: number) {
    this.update({ timeoutSeconds: Math.max(0, Math.trunc(seconds) || 0) })
  }

  step() {
    const { solution, played, puzzle, session } = this.state
    const direction = solution[played] as Direction | undefined
    if (!direction || !puzzle || !session) return

    this.update({ session: movePlayer(puzzle, session, direction) ?? session, played: played + 1 })
  }

  togglePlay() {
    const { playing, played, solution } = this.state
    this.update({ playing: !playing && played < solution.length })
  }

  reset() {
    const { puzzle } = this.state
    if (!puzzle) return

    this.update({ playing: false, played: 0, session: puzzle.start })
  }

  move(direction: Direction) {
    const { puzzle, session } = this.state
    const next = puzzle && session && movePlayer(puzzle, session, direction)
    if (!next) return

    this.update({ session: next, playing: false, solution: '', played: 0 })
  }

  undo() {
    const { session, played } = this.state
    if (!session) return

    this.update({ playing: false, session: undoMove(session), played: Math.max(0, played - 1) })
  }

  #endPolling() {
    this.#stopPolling?.()
    this.#stopPolling = null
    this.update({ solving: false })
  }

  #handleStatus(status: SolveStatus) {
    this.update({ notice: describeSolveOutcome(status) })
    if (status.state === 'running') return

    this.#endPolling()
    if (status.state !== 'done') return

    this.update({
      winnerThread: status.winnerThreadId,
      solution: status.solution,
      played: 0,
      playing: true,
    })
  }

  async solve() {
    const { puzzle, timeoutSeconds, thread, solverId } = this.state
    if (!puzzle) return

    this.reset()
    this.update({ playing: false, solution: '', played: 0 })

    const error = await this.#engine.startSolve(puzzle.text, timeoutSeconds || 0, thread, solverId)
    if (error) {
      this.update({ notice: { text: error, tone: 'bad' } })
      return
    }

    this.update({ solving: true, notice: { text: 'Solving... 0.0s', tone: '' } })
    this.#stopPolling = pollJob(
      () => this.#engine.solveStatus(),
      (status) => this.#handleStatus(status),
    )
  }

  async cancel() {
    this.#endPolling()
    this.#handleStatus(await this.#engine.cancelSolve())
  }

  async selectMap(name: string) {
    const [{ text }] = await Promise.all([
      this.#engine.loadMap(name),
      this.#stopPolling !== null && this.cancel(),
    ])
    const next = parsePuzzle(text)
    this.#options.onMapSelected?.(name)

    this.update({
      mapName: name,
      puzzle: next,
      session: next.start,
      winnerThread: null,
      playing: false,
      solution: '',
      played: 0,
      notice: next.warning ? { text: next.warning, tone: 'bad' } : IDLE,
    })
  }

  neighbourMap(offset: number) {
    const { maps, mapName } = this.state
    const next = maps[(maps.indexOf(mapName) + offset + maps.length) % maps.length]
    if (next) void this.selectMap(next)
  }

  /** Loads the maps and solvers, and opens `preferredMap`, or the first map when it is missing. */
  async init(preferredMap?: string | null, preferredSolver?: string | null) {
    const [{ maps, built }, solvers] = await Promise.all([
      this.#engine.listMaps(),
      this.#engine.listSolvers(),
    ])
    const preferred = preferredSolver && solvers.some((s) => s.id === preferredSolver)
    this.update({ maps, solvers, solverId: preferred ? preferredSolver : (solvers[0]?.id ?? '') })
    const first = preferredMap && maps.includes(preferredMap) ? preferredMap : maps[0]
    if (first) await this.selectMap(first)

    if (!built) {
      this.update({
        notice: {
          text: 'Native solver not built yet. Click Build or it will compile on Solve.',
          tone: 'bad',
        },
      })
    }
  }

  /** Stops timers. Call it when the user interface closes. */
  dispose() {
    this.#stopPolling?.()
    this.#stopPolling = null
    if (this.#playbackTimer !== null) clearTimeout(this.#playbackTimer)
    this.#playbackTimer = null
  }
}
