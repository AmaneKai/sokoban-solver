import type { Exploration } from '../domain/exploration.ts'
import type {
  BuildResult,
  ExploreStatus,
  MapList,
  MapText,
  SolveStatus,
  SolverInfo,
} from '../domain/contract.ts'

/**
 * The protocol between a user interface and the solver.
 * Every method that starts a job returns an error message, or null on success.
 */
export interface EngineService {
  listMaps(): Promise<MapList>
  loadMap(name: string): Promise<MapText>
  listSolvers(): Promise<SolverInfo[]>
  build(solver?: string): Promise<BuildResult>

  startSolve(map: string, timeoutSeconds: number, thread?: number, solver?: string): Promise<string | null>
  solveStatus(): Promise<SolveStatus>
  cancelSolve(): Promise<SolveStatus>

  startExplore(
    map: string,
    threadId: number,
    maxExpansions: number,
    solver?: string,
  ): Promise<string | null>
  exploreStatus(): Promise<ExploreStatus>
  stopExplore(): Promise<ExploreStatus>
  fetchExploration(): Promise<Exploration>
}
