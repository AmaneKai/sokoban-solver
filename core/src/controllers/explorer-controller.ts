import type { EngineService } from '../client/engine-service.ts'
import type { ExploreStatus, ExploreSummary, StopReason } from '../domain/contract.ts'
import { formatCount, IDLE, type Notice } from '../format.ts'
import { parsePuzzle, type Puzzle } from '../domain/puzzle.ts'
import {
  buildSearchTree,
  computeSubtreeSizes,
  neighbours,
  type Neighbours,
  type SearchTree,
} from '../domain/search-tree.ts'
import { Observable, pollJob } from './observable.ts'

const RUNNING_PLACEHOLDER =
  'Exploring... the tree appears here when the run finishes. Stop keeps what it has so far.'
const STOP_REASON_TEXT: Record<Exclude<StopReason, ''>, string> = {
  expansions: 'stopped at the expansion limit',
  time: 'stopped at the time limit',
  size: 'stopped: the tree grew too large to keep',
  stopped: 'stopped by you',
  pool: 'stopped: the solver ran out of state pool',
}

export function describeSummary(done: ExploreSummary): string {
  const ending = done.stopReason
    ? STOP_REASON_TEXT[done.stopReason]
    : done.isSolved
      ? 'solved'
      : 'search ended without a solution'
  const expansions = `${formatCount(done.expansionCount)} expansions`
  const states = `${formatCount(done.nodeCount)} states`

  return `thread ${done.threadId} · ${expansions} · ${states} · ${ending}`
}

export interface ExplorerState {
  mapName: string
  puzzle: Puzzle | null
  /** Solver whose tracer runs. Empty means the default one. */
  solverId: string
  thread: number
  maxExpansions: number
  running: boolean
  notice: Notice
  placeholder: string
  summary: ExploreSummary | null
  tree: SearchTree | null
  selected: number | null
  rejectedIndex: number | null
}

export interface ExplorerControllerOptions {
  /** Lets the user interface repaint between slow steps. Defaults to a zero-delay timer. */
  yieldToUi?: () => Promise<void>
}

/** Owns the state of the explorer view: the run, the search tree, and the selected state. */
export class ExplorerController extends Observable<ExplorerState> {
  readonly #engine: EngineService
  readonly #yieldToUi: () => Promise<void>
  #stopPolling: (() => void) | null = null
  #sizesTree: SearchTree | null = null
  #sizes: Uint32Array | null = null

  constructor(engine: EngineService, options: ExplorerControllerOptions = {}) {
    super({
      mapName: '',
      puzzle: null,
      solverId: '',
      thread: 0,
      maxExpansions: 0,
      running: false,
      notice: IDLE,
      placeholder: 'Press Run to record the search tree.',
      summary: null,
      tree: null,
      selected: null,
      rejectedIndex: null,
    })
    this.#engine = engine
    this.#yieldToUi = options.yieldToUi ?? (() => new Promise((resolve) => setTimeout(resolve, 0)))
  }

  /** Subtree size of every state. Computed once per tree. */
  get subtreeSizes(): Uint32Array | null {
    const { tree } = this.state
    if (!tree) return null
    if (this.#sizesTree !== tree) {
      this.#sizesTree = tree
      this.#sizes = computeSubtreeSizes(tree)
    }
    return this.#sizes
  }

  /** Neighbour states of the selected state. */
  get around(): Neighbours | null {
    const { tree, selected } = this.state
    return tree && selected !== null ? neighbours(tree, selected) : null
  }

  get summaryText(): string {
    const { summary } = this.state
    return summary ? describeSummary(summary) : ''
  }

  #say(text: string, tone: Notice['tone'] = '') {
    this.update({ notice: { text, tone } })
  }

  setNotice(notice: Notice) {
    this.update({ notice })
  }

  setSolver(id: string) {
    this.update({ solverId: id })
  }

  setThread(thread: number) {
    this.update({ thread: Math.max(0, Math.trunc(thread) || 0) })
  }

  setMaxExpansions(count: number) {
    this.update({ maxExpansions: Math.max(0, Math.trunc(count) || 0) })
  }

  select(index: number | null, rejected: number | null = null) {
    this.update({ selected: index, rejectedIndex: rejected })
  }

  goTo(key: keyof Neighbours) {
    const target = this.around?.[key] ?? null
    if (target !== null) this.select(target)
  }

  async openMap(name: string) {
    const { text } = await this.#engine.loadMap(name)
    this.update({
      mapName: name,
      puzzle: parsePuzzle(text),
      tree: null,
      summary: null,
      selected: null,
      rejectedIndex: null,
      placeholder: 'Press Run to record the search tree.',
    })
  }

  async #show(done: ExploreSummary) {
    this.#say(`Downloading ${formatCount(done.nodeCount)} states...`)
    await this.#yieldToUi()
    let built: SearchTree
    try {
      const exploration = await this.#engine.fetchExploration()
      this.#say('Laying out the tree...')
      await this.#yieldToUi()
      built = buildSearchTree(exploration)
    } catch (error) {
      this.#say(String(error), 'bad')
      return
    }

    this.update({ summary: done, tree: built })
    this.select(0)
    this.#say('Done.', 'ok')
  }

  async #handle(status: ExploreStatus) {
    if (status.state === 'running')
      return this.#say(`Exploring... ${status.elapsedSeconds.toFixed(1)}s`)

    this.update({ running: false })
    if (status.state === 'done' && status.summary) return this.#show(status.summary)

    const text = status.state === 'cancelled' ? 'Cancelled.' : `Explore failed: ${status.error}`
    this.#say(text, 'bad')
    this.update({ placeholder: text })
  }

  async run() {
    const { puzzle, thread, maxExpansions, solverId } = this.state
    if (!puzzle) return

    const error = await this.#engine.startExplore(puzzle.text, thread, maxExpansions, solverId || undefined)
    if (error) return this.#say(error, 'bad')

    this.update({
      running: true,
      tree: null,
      summary: null,
      selected: null,
      rejectedIndex: null,
      placeholder: RUNNING_PLACEHOLDER,
    })
    this.#say('Exploring... 0.0s')
    this.#stopPolling = pollJob(
      () => this.#engine.exploreStatus(),
      (status) => void this.#handle(status),
    )
  }

  async stop() {
    this.#say('Stopping... (keeps the tree built so far)')
    await this.#engine.stopExplore()
  }

  setPlaceholder(text: string) {
    this.update({ placeholder: text })
  }

  /** Stops timers. Call it when the user interface closes. */
  dispose() {
    this.#stopPolling?.()
    this.#stopPolling = null
  }
}
