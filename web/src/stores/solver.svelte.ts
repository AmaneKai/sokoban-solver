import { HttpEngineService, SolverController, type Direction, type Notice } from '@sokobot/core'

const SAVED_MAP_KEY = 'map'
const SAVED_SOLVER_KEY = 'solver'

function saved(key: string): string | null {
  try {
    return localStorage.getItem(key)
  } catch {
    return null
  }
}

function save(key: string, value: string) {
  try {
    localStorage.setItem(key, value)
  } catch {
    // Storage can be blocked. The choice is then not remembered.
  }
}

export const engine = new HttpEngineService()

/** Svelte 5 reactive wrapper around SolverController. */
class SolverStore {
  readonly controller = new SolverController(engine, {
    onMapSelected: (name) => save(SAVED_MAP_KEY, name),
    onSolverSelected: (id) => save(SAVED_SOLVER_KEY, id),
  })
  #snapshot = $state.raw(this.controller.state)

  constructor() {
    this.controller.subscribe(() => (this.#snapshot = this.controller.state))
  }

  get maps() {
    return this.#snapshot.maps
  }
  get solvers() {
    return this.#snapshot.solvers
  }
  get solverId() {
    return this.#snapshot.solverId
  }
  set solverId(value: string) {
    this.controller.setSolver(value)
  }
  /** The selected solver, or undefined while the list is loading. */
  get solverInfo() {
    return this.#snapshot.solvers.find((info) => info.id === this.#snapshot.solverId)
  }
  get mapName() {
    return this.#snapshot.mapName
  }
  get puzzle() {
    return this.#snapshot.puzzle
  }
  get session() {
    return this.#snapshot.session
  }
  get solution() {
    return this.#snapshot.solution
  }
  get played() {
    return this.#snapshot.played
  }
  get playing() {
    return this.#snapshot.playing
  }
  get solving() {
    return this.#snapshot.solving
  }
  get winnerThread() {
    return this.#snapshot.winnerThread
  }
  get notice() {
    return this.#snapshot.notice
  }
  set notice(value: Notice) {
    this.controller.setNotice(value)
  }
  pause = () => this.controller.pause()
  get speed() {
    return this.#snapshot.speed
  }
  set speed(value: number) {
    this.controller.setSpeed(value)
  }
  get thread() {
    return this.#snapshot.thread
  }
  set thread(value: number) {
    this.controller.setThread(value)
  }
  get timeoutSeconds() {
    return this.#snapshot.timeoutSeconds
  }
  set timeoutSeconds(value: number) {
    this.controller.setTimeoutSeconds(value)
  }

  step = () => this.controller.step()
  togglePlay = () => this.controller.togglePlay()
  reset = () => this.controller.reset()
  move = (direction: Direction) => this.controller.move(direction)
  undo = () => this.controller.undo()
  solve = () => this.controller.solve()
  cancel = () => this.controller.cancel()
  selectMap = (name: string) => this.controller.selectMap(name)
  neighbourMap = (offset: number) => this.controller.neighbourMap(offset)
  init = () => this.controller.init(saved(SAVED_MAP_KEY), saved(SAVED_SOLVER_KEY))
}

export const solver = new SolverStore()
