import { mkdirSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import type { SearchTree } from '../domain/search-tree.ts'
import { depthProfile, depthProfileCsv, progressCsv, progressCurve } from '../domain/tree-profile.ts'

const PROGRESS_POINTS = 400
const pad = (value: number) => String(value).padStart(2, '0')

/** Writes the depth profile, the progress curve, and the summary of a run. Returns the file paths. */
export function writeTelemetryFiles(dataDir: string, mapName: string, tree: SearchTree): string[] {
  const now = new Date()
  const day = `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`
  const time = `${pad(now.getHours())}-${pad(now.getMinutes())}-${pad(now.getSeconds())}`
  const folder = `${day}_${time}_${mapName}`
  const dir = join(dataDir, folder)
  mkdirSync(dir, { recursive: true })

  const exports: [string, string][] = [
    ['depth-profile.csv', depthProfileCsv(depthProfile(tree))],
    ['search-progress.csv', progressCsv(progressCurve(tree, PROGRESS_POINTS))],
    ['summary.json', JSON.stringify(tree.exploration.summary, null, 2)],
  ]

  return exports.map(([suffix, text]) => {
    const dot = suffix.indexOf('.')
    const name = `${mapName}-${suffix.slice(0, dot)}-${time}${suffix.slice(dot)}`
    writeFileSync(join(dir, name), text)
    return join(folder, name)
  })
}
