export type Listener = () => void

/**
 * Holds an immutable state object. Each change replaces the object and notifies listeners.
 */
export abstract class Observable<State extends object> {
  #state: State
  readonly #listeners = new Set<Listener>()

  protected constructor(initial: State) {
    this.#state = initial
  }

  get state(): State {
    return this.#state
  }

  /** Registers a listener. The returned function removes it. */
  subscribe(listener: Listener): () => void {
    this.#listeners.add(listener)
    return () => void this.#listeners.delete(listener)
  }

  protected update(patch: Partial<State>): void {
    this.#state = { ...this.#state, ...patch }
    this.onUpdate(patch)
    for (const listener of this.#listeners) listener()
  }

  /** Called after each change and before listeners run. */
  protected onUpdate(_patch: Partial<State>): void {}
}

const JOB_POLL_INTERVAL_MS = 250

/** Polls a job until it leaves the running state. The returned function stops the polling. */
export function pollJob<Status extends { state: string }>(
  fetchStatus: () => Promise<Status>,
  onStatus: (status: Status) => void,
  intervalMs = JOB_POLL_INTERVAL_MS,
): () => void {
  const timer = setInterval(async () => {
    const status = await fetchStatus()
    if (status.state !== 'running') clearInterval(timer)

    onStatus(status)
  }, intervalMs)

  return () => clearInterval(timer)
}
