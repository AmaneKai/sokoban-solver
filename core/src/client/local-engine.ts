import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import type {
  BuildResult,
  ExploreStatus,
  ExploreSummary,
  JobState,
  MapList,
  MapText,
  SolveStatus,
  SolverInfo,
} from '../domain/contract.ts'
import { decodeExploration, type Exploration } from '../domain/exploration.ts'
import type { EngineService } from './engine-service.ts'

const STOP_GRACE_MS = 15_000
const WINNER_LINE = /\[solver\] thread (\d+) won \(\d+ moves, (\d+) nodes(?:, backward)?\)/
const collator = new Intl.Collator('en', { numeric: true, sensitivity: 'base' })

type Process = ReturnType<typeof Bun.spawn>

const readText = (stream: ReadableStream | number | undefined) =>
  stream instanceof ReadableStream ? new Response(stream).text() : Promise.resolve('')

/** One native process at a time: start it, poll its snapshot, stop (graceful) or cancel (kill). */
function createJob<Payload>(
  interpret: (stdout: Buffer, stderr: string) => Payload,
  isSuccess: (payload: Payload) => boolean,
  env: Record<string, string> = {},
) {
  let state: JobState = 'idle'
  let error = ''
  let payload: Payload | null = null
  let started = 0
  let elapsed = 0
  let runId = 0
  let child: Process | null = null

  const isRunning = () => state === 'running'
  const isCancelled = () => state === 'cancelled'
  const seconds = () => (Date.now() - started) / 1000

  async function start(
    stdin: string,
    args: string[],
    timeoutSeconds: number,
    extraEnv: Record<string, string>,
    binary: string,
  ) {
    const id = ++runId
    state = 'running'
    error = ''
    payload = null
    started = Date.now()

    let timedOut = false
    let timer: ReturnType<typeof setTimeout> | undefined
    let proc: Process
    try {
      proc = Bun.spawn([binary, ...args], {
        stdin: Buffer.from(stdin),
        env: { ...process.env, ...env, ...extraEnv },
        stdout: 'pipe',
        stderr: 'pipe',
      })
    } catch (cause) {
      state = 'failed'
      error = String(cause)
      return
    }
    child = proc

    if (timeoutSeconds > 0) {
      timer = setTimeout(() => {
        timedOut = true
        proc.kill('SIGKILL')
      }, timeoutSeconds * 1000)
    }

    const [stdout, stderr] = await Promise.all([
      new Response(proc.stdout as ReadableStream).arrayBuffer(),
      readText(proc.stderr as ReadableStream),
      proc.exited,
    ])
    clearTimeout(timer)
    if (id !== runId || isCancelled()) return

    child = null
    elapsed = seconds()

    if (timedOut) state = 'timeout'
    else if (proc.exitCode !== 0) {
      state = 'failed'
      error = stderr.trim() || `exited with ${proc.exitCode ?? proc.signalCode}`
    } else {
      try {
        payload = interpret(Buffer.from(stdout), stderr)
        state = isSuccess(payload) ? 'done' : 'unsolved'
      } catch (cause) {
        state = 'failed'
        error = String(cause)
      }
    }
  }

  return {
    isRunning,
    start: (
      stdin: string,
      args: string[],
      timeoutSeconds: number,
      binary: string,
      extraEnv: Record<string, string> = {},
    ) => void start(stdin, args, timeoutSeconds, extraEnv, binary),
    stop() {
      const proc = child
      if (!isRunning() || !proc) return

      proc.kill('SIGTERM')
      setTimeout(() => proc.kill('SIGKILL'), STOP_GRACE_MS).unref()
    },
    cancel() {
      if (!isRunning()) return

      state = 'cancelled'
      elapsed = seconds()
      child?.kill('SIGKILL')
      child = null
    },
    snapshot: () => ({
      state,
      error,
      elapsedSeconds: Math.round((isRunning() ? seconds() : elapsed) * 100) / 100,
      payload,
    }),
  }
}

/** What a solver folder declares in its optional `solver.json`. */
interface SolverConfig {
  name?: string
  /** Name of the solve binary in `bin/`. */
  solve?: string
  /** Name of the tracer binary in `bin/`. Without one the solver cannot be explored. */
  trace?: string
  /** Make targets that build the binaries. Defaults to `solve` plus `trace` when there is a tracer. */
  targets?: string[]
  /** Extra command line arguments for the solve binary. */
  args?: string[]
  env?: Record<string, string>
  /** True when the solver reads SOKO_THREADS. */
  threads?: boolean
  /** `ms-moves`: a time line and then the moves. `moves`: only the moves. */
  output?: 'ms-moves' | 'moves'
  /** Operating systems the solver builds on, as in `process.platform`. Without it, every system. */
  platform?: string | string[]
}

interface Solver {
  id: string
  name: string
  dir: string
  solveBinary: string
  traceBinary: string | null
  targets: string[]
  args: string[]
  env: Record<string, string>
  threads: boolean
  output: 'ms-moves' | 'moves'
}

const DEFAULT_SOLVER = 'portfolio'

export interface LocalEngineOptions {
  /** Repository root that holds `solvers/` and `maps/`. Defaults to the root above `core/`. */
  rootDir?: string
}

const toInt = (value: unknown) => Math.max(0, Math.trunc(Number(value)) || 0)

/** Runs the native binaries as child processes of the current process. */
export class LocalEngineService implements EngineService {
  readonly solversDir: string
  readonly mapsDir: string

  readonly #make: string
  readonly #win: boolean
  readonly #solveJob
  readonly #exploreJob
  #solveOutput: Solver['output'] = 'ms-moves'

  constructor(options: LocalEngineOptions = {}) {
    // import.meta.url (not .dir): Vite rewrites it to the real file when it bundles a config.
    const rootDir =
      options.rootDir ?? join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..')
    this.#win = process.platform === 'win32'

    this.solversDir = join(rootDir, 'solvers')
    this.mapsDir = join(rootDir, 'maps')
    this.#make = this.#win ? 'mingw32-make' : 'make'

    this.#solveJob = createJob(
      (stdout, stderr) => {
        const lines = stdout.toString('utf8').split('\n')
        const [ms = '', moves = ''] = this.#solveOutput === 'moves' ? ['', lines[0]] : lines
        const winner = WINNER_LINE.exec(stderr)
        return {
          solution: moves.trim(),
          solverMilliseconds: Number.isFinite(parseFloat(ms)) ? parseFloat(ms) : null,
          winnerThreadId: winner ? Number(winner[1]) : null,
          winnerNodeCount: winner ? Number(winner[2]) : null,
        }
      },
      (result) => result.solution !== '',
    )

    this.#exploreJob = createJob(
      (stdout) => {
        const headerLength = stdout.readUInt32LE(0)
        const summary = JSON.parse(stdout.toString('utf8', 4, 4 + headerLength)) as ExploreSummary
        return { summary, tree: stdout }
      },
      () => true,
    )
  }

  /** Every folder in `solvers/` that has a Makefile. Read on each call, so a new folder shows up at once. */
  #solvers(): Solver[] {
    if (!existsSync(this.solversDir)) return []

    const binary = (dir: string, name: string) =>
      join(dir, 'bin', this.#win ? `${name}.exe` : name)

    return readdirSync(this.solversDir)
      .filter((name) => existsSync(join(this.solversDir, name, 'Makefile')))
      .flatMap((name): Solver[] => {
        const dir = join(this.solversDir, name)
        const file = join(dir, 'solver.json')
        const config = (existsSync(file) ? JSON.parse(readFileSync(file, 'utf8')) : {}) as SolverConfig
        if (config.platform && ![config.platform].flat().includes(process.platform)) return []

        return [{
          id: name,
          name: config.name ?? name,
          dir,
          solveBinary: binary(dir, config.solve ?? 'sokosolve'),
          traceBinary: config.trace ? binary(dir, config.trace) : null,
          targets: config.targets ?? ['solve', ...(config.trace ? ['trace'] : [])],
          args: config.args ?? [],
          env: config.env ?? {},
          threads: config.threads ?? false,
          output: config.output ?? 'ms-moves',
        }]
      })
      .sort((a, b) => collator.compare(a.id, b.id))
  }

  /** The solver with this id, or the default one when `id` is empty. */
  #solver(id?: string): Solver | string {
    const all = this.#solvers()
    const found = id ? all.find((solver) => solver.id === id) : (all.find((solver) => solver.id === DEFAULT_SOLVER) ?? all[0])
    return found ?? (id ? `no such solver: ${id}` : 'no solvers found in solvers/')
  }

  #mapNames() {
    return readdirSync(this.mapsDir)
      .filter((file) => file.endsWith('.txt'))
      .map((file) => file.slice(0, -4))
      .sort(collator.compare)
  }

  async #runMake(solver: Solver, targets: string[]) {
    const proc = Bun.spawn([this.#make, ...targets], {
      cwd: solver.dir,
      stdout: 'ignore',
      stderr: 'pipe',
    })
    const stderr = await new Response(proc.stderr).text()

    return (await proc.exited) === 0 ? null : stderr.trim() || `make exited with ${proc.exitCode}`
  }

  /** Checks the preconditions of a job and builds the solver when its binary is missing. */
  async #prepare(solver: Solver, binary: string): Promise<string | null> {
    if (this.#solveJob.isRunning() || this.#exploreJob.isRunning())
      return 'a job is currently running; wait for it to finish'

    const buildError = existsSync(binary) ? null : await this.#runMake(solver, solver.targets)
    return buildError ? `Auto-build failed: ${buildError}` : null
  }

  async listMaps(): Promise<MapList> {
    const solver = this.#solver()
    return {
      maps: this.#mapNames(),
      built: typeof solver !== 'string' && this.#isBuilt(solver),
    }
  }

  #isBuilt(solver: Solver) {
    return existsSync(solver.solveBinary) && (!solver.traceBinary || existsSync(solver.traceBinary))
  }

  async listSolvers(): Promise<SolverInfo[]> {
    return this.#solvers().map((solver) => ({
      id: solver.id,
      name: solver.name,
      built: this.#isBuilt(solver),
      explorable: solver.traceBinary !== null,
      threads: solver.threads,
    }))
  }

  async loadMap(name: string): Promise<MapText> {
    if (!this.#mapNames().includes(name)) throw new Error('no such map')

    return { name, text: readFileSync(join(this.mapsDir, `${name}.txt`), 'latin1') }
  }

  async build(id?: string): Promise<BuildResult> {
    const solver = this.#solver(id)
    if (typeof solver === 'string') return { ok: false, error: solver }

    const error = await this.#runMake(solver, ['clean', ...solver.targets])
    return error ? { ok: false, error } : { ok: true }
  }

  /** `thread` < 0 runs the whole portfolio; otherwise only that thread config. */
  async startSolve(
    map: string,
    timeoutSeconds: number,
    thread = -1,
    solverId?: string,
  ): Promise<string | null> {
    const solver = this.#solver(solverId)
    if (typeof solver === 'string') return solver

    const error = await this.#prepare(solver, solver.solveBinary)
    if (error) return error
    if (typeof map !== 'string' || map === '') return 'map required'

    const only = Math.trunc(Number(thread))
    // The default caps threads at the performance-core count, which can drop the last thread.
    // Run the full portfolio unless the user set SOKO_THREADS.
    const threadEnv: Record<string, string> = !solver.threads
      ? {}
      : only >= 0
        ? { SOKO_THREADS: String(only) }
        : { SOKO_THREADS: process.env.SOKO_THREADS ?? '0,1,2,3,4' }

    this.#solveOutput = solver.output
    this.#solveJob.start(map, solver.args, toInt(timeoutSeconds), solver.solveBinary, {
      ...solver.env,
      ...threadEnv,
    })
    return null
  }

  async solveStatus(): Promise<SolveStatus> {
    const { payload, ...rest } = this.#solveJob.snapshot()
    return {
      ...rest,
      solution: payload?.solution ?? '',
      solverMilliseconds: payload?.solverMilliseconds ?? null,
      winnerThreadId: payload?.winnerThreadId ?? null,
      winnerNodeCount: payload?.winnerNodeCount ?? null,
    }
  }

  async cancelSolve(): Promise<SolveStatus> {
    this.#solveJob.cancel()
    return this.solveStatus()
  }

  /** Only a solver with a tracer can be explored. With no solver named, the default one is used. */
  async startExplore(
    map: string,
    threadId: number,
    maxExpansions: number,
    solverId?: string,
  ): Promise<string | null> {
    const solver = this.#solver(solverId)
    if (typeof solver === 'string') return solver
    if (!solver.traceBinary) return `${solver.name} has no explorer`

    const error = await this.#prepare(solver, solver.traceBinary)
    if (error) return error
    if (typeof map !== 'string' || map === '') return 'map required'

    const args = ['--thread', String(toInt(threadId)), '--pops', String(toInt(maxExpansions))]
    this.#exploreJob.start(map, args, 0, solver.traceBinary)
    return null
  }

  async exploreStatus(): Promise<ExploreStatus> {
    const { payload, ...rest } = this.#exploreJob.snapshot()
    return { ...rest, summary: payload?.summary ?? null }
  }

  async stopExplore(): Promise<ExploreStatus> {
    this.#exploreJob.stop()
    return this.exploreStatus()
  }

  /** The raw binary trace of the last run, or null when no run has finished. */
  exploreResultBytes(): Uint8Array<ArrayBuffer> | null {
    const tree = this.#exploreJob.snapshot().payload?.tree
    // Buffer -> Uint8Array<ArrayBuffer> without copying a multi-MB tree.
    return tree ? (tree as unknown as Uint8Array<ArrayBuffer>) : null
  }

  async fetchExploration(): Promise<Exploration> {
    const bytes = this.exploreResultBytes()
    if (!bytes) throw new Error('no explore result available')

    const { buffer, byteOffset, byteLength } = bytes
    return decodeExploration(buffer.slice(byteOffset, byteOffset + byteLength))
  }
}
