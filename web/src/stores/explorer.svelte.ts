import { ExplorerController, type Neighbours, type Notice } from '@sokobot/core'
import { engine } from './solver.svelte.ts'

const nextPaint = () => new Promise<void>((resolve) => requestAnimationFrame(() => resolve()))

/** Svelte 5 reactive wrapper around ExplorerController. */
class ExploreStore {
  readonly controller = new ExplorerController(engine, { yieldToUi: nextPaint })
  #snapshot = $state.raw(this.controller.state)

  constructor() {
    this.controller.subscribe(() => (this.#snapshot = this.controller.state))
  }

  get mapName() {
    return this.#snapshot.mapName
  }
  get puzzle() {
    return this.#snapshot.puzzle
  }
  get running() {
    return this.#snapshot.running
  }
  get notice() {
    return this.#snapshot.notice
  }
  set notice(value: Notice) {
    this.controller.setNotice(value)
  }
  get placeholder() {
    return this.#snapshot.placeholder
  }
  get summary() {
    return this.#snapshot.summary
  }
  get tree() {
    return this.#snapshot.tree
  }
  get selected() {
    return this.#snapshot.selected
  }
  get rejectedIndex() {
    return this.#snapshot.rejectedIndex
  }
  get solverId() {
    return this.#snapshot.solverId
  }
  get thread() {
    return this.#snapshot.thread
  }
  set thread(value: number) {
    this.controller.setThread(value)
  }
  get maxExpansions() {
    return this.#snapshot.maxExpansions
  }
  set maxExpansions(value: number) {
    this.controller.setMaxExpansions(value)
  }

  get subtreeSizes() {
    void this.#snapshot.tree
    return this.controller.subtreeSizes
  }
  get summaryText() {
    void this.#snapshot.summary
    return this.controller.summaryText
  }

  select = (index: number | null, rejected: number | null = null) =>
    this.controller.select(index, rejected)
  goTo = (key: keyof Neighbours) => this.controller.goTo(key)
  run = () => this.controller.run()
  stop = () => this.controller.stop()

  async init(params: URLSearchParams) {
    let name = params.get('map')
    try {
      name ??= localStorage.getItem('map')
    } catch {
      // Storage can be blocked. Fall through to the placeholder.
    }
    if (!name) {
      this.controller.setPlaceholder('No map selected. Pick one on the solver page first.')
      return
    }

    this.controller.setSolver(params.get('solver') ?? '')
    this.controller.setThread(Number(params.get('thread') ?? 0))
    this.controller.setMaxExpansions(Number(params.get('pops') ?? 0))
    await this.controller.openMap(name)
    if (params.get('autorun') === '1') await this.controller.run()
  }
}

export const explorer = new ExploreStore()
