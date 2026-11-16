import type { Exploration } from '../domain/exploration.ts'
import { decodeExploration } from '../domain/exploration.ts'
import type {
  BuildResult,
  ExploreStatus,
  MapList,
  MapText,
  SolveStatus,
  SolverInfo,
} from '../domain/contract.ts'
import type { EngineService } from './engine-service.ts'

/** Talks to the HTTP API of the web server with `fetch`. */
export class HttpEngineService implements EngineService {
  constructor(private readonly baseUrl = '') {}

  async #request<Response>(path: string, body?: unknown): Promise<Response> {
    const init: RequestInit | undefined =
      body === undefined
        ? undefined
        : {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
          }

    const response = await fetch(this.baseUrl + path, init)
    if (!response.ok && !response.headers.get('content-type')?.includes('json'))
      throw new Error(`${path}: HTTP ${response.status}`)

    return response.json() as Promise<Response>
  }

  async #startJob(path: string, body: unknown): Promise<string | null> {
    const response = await this.#request<{ error?: string }>(path, body)
    return response.error || null
  }

  listMaps = () => this.#request<MapList>('/api/maps')
  loadMap = (name: string) => this.#request<MapText>(`/api/maps/${encodeURIComponent(name)}`)
  listSolvers = () => this.#request<SolverInfo[]>('/api/solvers')
  build = (solver?: string) => this.#request<BuildResult>('/api/build', { solver })

  startSolve = (map: string, timeoutSeconds: number, thread = -1, solver?: string) =>
    this.#startJob('/api/solve', { map, timeout: timeoutSeconds, thread, solver })
  solveStatus = () => this.#request<SolveStatus>('/api/solve/status')
  cancelSolve = () => this.#request<SolveStatus>('/api/solve/cancel', {})

  startExplore = (map: string, threadId: number, maxExpansions: number, solver?: string) =>
    this.#startJob('/api/explore', { map, thread: threadId, pops: maxExpansions, solver })
  exploreStatus = () => this.#request<ExploreStatus>('/api/explore/status')
  stopExplore = () => this.#request<ExploreStatus>('/api/explore/stop', {})

  async fetchExploration(): Promise<Exploration> {
    const response = await fetch(`${this.baseUrl}/api/explore/result`)
    if (!response.ok) throw new Error(`No explore result (HTTP ${response.status})`)

    return decodeExploration(await response.arrayBuffer())
  }
}
