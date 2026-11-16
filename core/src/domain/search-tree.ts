import { FATE_EXPANDED, FATE_SOLUTION, type Exploration } from './exploration.ts'

export interface SearchTree {
  readonly exploration: Exploration
  readonly nodeCount: number
  readonly depth: Int32Array
  readonly row: Float32Array
  readonly childStart: Int32Array
  readonly childList: Int32Array
  readonly maxDepth: number
  readonly rowCount: number
  readonly solutionPath: Int32Array
  readonly detailOrder: Int32Array
}

function buildChildLists(parentIndex: Int32Array) {
  const nodeCount = parentIndex.length
  const childStart = new Int32Array(nodeCount + 1)

  for (let index = 1; index < nodeCount; index++) {
    const parent = parentIndex[index]!
    childStart[parent + 1]! += 1
  }
  for (let index = 0; index < nodeCount; index++) {
    childStart[index + 1]! += childStart[index]!
  }

  const childList = new Int32Array(Math.max(nodeCount - 1, 0))
  const fill = childStart.slice(0, nodeCount)

  for (let index = 1; index < nodeCount; index++) {
    const parent = parentIndex[index]!
    childList[fill[parent]!] = index
    fill[parent]! += 1
  }

  return { childStart, childList }
}

const MAX_LANE_OFFSET = 1 << 20
const LANE_SEARCH_WINDOW = 48
const LANE_GAP = 1
const NEVER_BUSY = -10

function layoutLanes(
  parentIndex: Int32Array,
  childStart: Int32Array,
  depth: Int32Array,
  maxDepth: number,
  solutionPath: Int32Array,
) {
  const nodeCount = parentIndex.length
  const size = new Uint32Array(nodeCount).fill(1)

  for (let index = nodeCount - 1; index > 0; index--) {
    const parent = parentIndex[index]!
    size[parent]! += size[index]!
  }

  const heavyChild = new Int32Array(nodeCount).fill(-1)
  for (let index = 1; index < nodeCount; index++) {
    const parent = parentIndex[index]!
    const current = heavyChild[parent]!
    if (current < 0 || size[index]! > size[current]!) heavyChild[parent] = index
  }
  for (let step = 0; step + 1 < solutionPath.length; step++) {
    heavyChild[solutionPath[step]!] = solutionPath[step + 1]!
  }

  let leafCount = 0
  for (let index = 0; index < nodeCount; index++) {
    if (childStart[index] === childStart[index + 1]) leafCount++
  }

  const pathOf = new Int32Array(nodeCount)
  const pathHead = new Int32Array(leafCount)
  const pathEnd = new Int32Array(leafCount)
  let pathCount = 0

  for (let index = 0; index < nodeCount; index++) {
    const parent = parentIndex[index]!
    if (parent >= 0 && heavyChild[parent] === index) {
      const path = pathOf[parent]!
      pathOf[index] = path
      pathEnd[path] = depth[index]!
      continue
    }

    pathOf[index] = pathCount
    pathHead[pathCount] = index
    pathEnd[pathCount] = depth[index]!
    pathCount++
  }

  const startCounts = new Int32Array(maxDepth + 2)
  for (let path = 0; path < pathCount; path++) {
    const level = depth[pathHead[path]!]! + 1
    startCounts[level]! += 1
  }
  for (let level = 0; level <= maxDepth; level++) {
    startCounts[level + 1]! += startCounts[level]!
  }

  const fill = startCounts.slice(0, maxDepth + 1)
  const order = new Int32Array(pathCount)
  for (let path = 0; path < pathCount; path++) {
    const level = depth[pathHead[path]!]!
    order[fill[level]!] = path
    fill[level]! += 1
  }
  for (let level = 0; level <= maxDepth; level++) {
    order
      .subarray(startCounts[level]!, startCounts[level + 1]!)
      .sort((first, second) => pathEnd[second]! - pathEnd[first]!)
  }

  const busyUntil = new Int32Array(MAX_LANE_OFFSET * 2 + 1).fill(NEVER_BUSY)
  const rowOfPath = new Int32Array(pathCount)
  const nextSide = new Int8Array(pathCount).fill(1)
  let lowestRow = 0
  let highestRow = 0
  const isFree = (row: number, start: number) =>
    Math.abs(row) <= MAX_LANE_OFFSET && busyUntil[row + MAX_LANE_OFFSET]! < start - LANE_GAP

  for (const path of order) {
    const head = pathHead[path]!
    const start = depth[head]!
    const parentPath = head === 0 ? -1 : pathOf[parentIndex[head]!]!
    const baseRow = parentPath < 0 ? 0 : rowOfPath[parentPath]!
    const side = parentPath < 0 ? 1 : nextSide[parentPath]!

    if (parentPath >= 0) nextSide[parentPath] = -side

    let row = parentPath < 0 ? 0 : Number.NaN
    for (let offset = 1; Number.isNaN(row) && offset <= LANE_SEARCH_WINDOW; offset++) {
      if (isFree(baseRow + side * offset, start)) row = baseRow + side * offset
      else if (isFree(baseRow - side * offset, start)) row = baseRow - side * offset
    }

    if (Number.isNaN(row)) row = side > 0 ? highestRow + 1 : lowestRow - 1

    rowOfPath[path] = row
    busyUntil[Math.min(Math.max(row, -MAX_LANE_OFFSET), MAX_LANE_OFFSET) + MAX_LANE_OFFSET] =
      pathEnd[path]!
    lowestRow = Math.min(lowestRow, row)
    highestRow = Math.max(highestRow, row)
  }

  const rows = new Float32Array(nodeCount)
  for (let index = 0; index < nodeCount; index++) {
    rows[index] = rowOfPath[pathOf[index]!]! - lowestRow
  }

  return { rows, rowCount: highestRow - lowestRow + 1 }
}

function findSolutionPath(exploration: Exploration): Int32Array {
  const solutionIndex = exploration.fate.indexOf(FATE_SOLUTION)
  if (solutionIndex < 0) return new Int32Array(0)

  const path: number[] = []
  for (let index = solutionIndex; index >= 0; index = exploration.parentIndex[index]!) {
    path.push(index)
  }

  return Int32Array.from(path.reverse())
}

// counting sort: O(n) instead of a comparator sort over millions of detail rows
function sortDetailsByParent(detailParent: Int32Array, nodeCount: number): Int32Array {
  const start = new Int32Array(nodeCount + 1)
  for (const parent of detailParent) start[parent + 1]! += 1
  for (let index = 0; index < nodeCount; index++) start[index + 1]! += start[index]!

  const order = new Int32Array(detailParent.length)
  for (let detail = 0; detail < detailParent.length; detail++) {
    const parent = detailParent[detail]!
    order[start[parent]!] = detail
    start[parent]! += 1
  }

  return order
}

export function buildSearchTree(exploration: Exploration): SearchTree {
  const nodeCount = exploration.summary.nodeCount
  const { childStart, childList } = buildChildLists(exploration.parentIndex)

  const depth = new Int32Array(nodeCount)
  let maxDepth = 0
  for (let index = 1; index < nodeCount; index++) {
    const value = depth[exploration.parentIndex[index]!]! + 1
    depth[index] = value
    if (value > maxDepth) maxDepth = value
  }

  const solutionPath = findSolutionPath(exploration)
  const { rows, rowCount } = layoutLanes(
    exploration.parentIndex,
    childStart,
    depth,
    maxDepth,
    solutionPath,
  )

  const detailOrder = sortDetailsByParent(exploration.detailParent, nodeCount)

  return {
    exploration,
    nodeCount,
    depth,
    row: rows,
    childStart,
    childList,
    maxDepth,
    rowCount,
    solutionPath,
    detailOrder,
  }
}

export const childrenOf = (tree: SearchTree, index: number): Int32Array =>
  tree.childList.subarray(tree.childStart[index]!, tree.childStart[index + 1]!)

export const childCountOf = (tree: SearchTree, index: number) =>
  tree.childStart[index + 1]! - tree.childStart[index]!

function siblingsOf(tree: SearchTree, index: number) {
  const parent = tree.exploration.parentIndex[index]!
  if (parent < 0) return { previous: null, next: null }

  const siblings = childrenOf(tree, parent)
  const position = siblings.indexOf(index)

  return { previous: siblings[position - 1] ?? null, next: siblings[position + 1] ?? null }
}

export function computeSubtreeSizes(tree: SearchTree): Uint32Array {
  const sizes = new Uint32Array(tree.nodeCount).fill(1)
  for (let index = tree.nodeCount - 1; index > 0; index--) {
    const parent = tree.exploration.parentIndex[index]!
    if (parent >= 0) sizes[parent]! += sizes[index]!
  }
  return sizes
}

function nextDeadEnd(tree: SearchTree, from: number): number | null {
  const { fate } = tree.exploration
  for (let step = 1; step <= tree.nodeCount; step++) {
    const index = (from + step) % tree.nodeCount
    if (fate[index] === FATE_EXPANDED && childCountOf(tree, index) === 0) return index
  }
  return null
}

export function routeTo(tree: SearchTree, index: number): number[] {
  const route: number[] = []
  for (let step = index; step >= 0; step = tree.exploration.parentIndex[step]!) {
    route.push(step)
  }
  return route.reverse()
}

export interface Neighbours {
  root: number | null
  parent: number | null
  previous: number | null
  next: number | null
  child: number | null
  solution: number | null
  deadEnd: number | null
}

export function neighbours(tree: SearchTree, index: number): Neighbours {
  const parent = tree.exploration.parentIndex[index] ?? -1
  const { previous, next } = siblingsOf(tree, index)

  return {
    root: index === 0 ? null : 0,
    parent: parent < 0 ? null : parent,
    previous,
    next,
    child: childrenOf(tree, index)[0] ?? null,
    solution: tree.solutionPath[tree.solutionPath.length - 1] ?? null,
    deadEnd: nextDeadEnd(tree, index),
  }
}
