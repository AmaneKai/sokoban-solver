import type { ExploreSummary } from './contract.ts'

export const PRUNE_REASONS = [
  'none',
  'outsideCorral',
  'deadSquare',
  'twoByTwo',
  'freeze',
  'duplicate',
  'deadHeuristic',
  'piCorralDeadlock',
  'hallInfeasible',
] as const

export type PruneReason = (typeof PRUNE_REASONS)[number]

export const FATE_OPEN = 0
export const FATE_EXPANDED = 1
export const FATE_SOLUTION = 2
export const FATE_DROPPED = 3

export type Cell = readonly [row: number, column: number]

export interface Push {
  from: Cell
  to: Cell
}

export interface PrunedPush {
  push: Push
  reason: PruneReason
}

export interface Exploration {
  summary: ExploreSummary
  parentIndex: Int32Array
  move: Uint32Array
  pushCount: Uint32Array
  heuristic: Uint32Array
  expansionOrder: Uint32Array
  stepMicroseconds: Uint32Array
  fate: Uint8Array
  dropReason: Uint8Array
  rejectedCounts: Uint8Array
  detailParent: Int32Array
  detailMove: Uint32Array
  detailReason: Uint8Array
}

export const ROOT_MOVE = 0xffffffff

export function unpackPush(move: number): Push | null {
  if (move === ROOT_MOVE) return null

  return {
    from: [move >>> 24, (move >>> 16) & 0xff],
    to: [(move >>> 8) & 0xff, move & 0xff],
  }
}

const alignToFourBytes = (offset: number) => (offset + 3) & ~3

export function decodeExploration(buffer: ArrayBuffer): Exploration {
  const headerLength = new DataView(buffer).getUint32(0, true)
  const headerBytes = new Uint8Array(buffer, 4, headerLength)
  const summary = JSON.parse(new TextDecoder().decode(headerBytes)) as ExploreSummary

  let offset = alignToFourBytes(4 + headerLength)
  const nextArray = <Typed extends Int32Array | Uint32Array | Uint8Array>(
    ArrayType: {
      new (buffer: ArrayBuffer, offset: number, length: number): Typed
      BYTES_PER_ELEMENT: number
    },
    length: number,
  ): Typed => {
    const array = new ArrayType(buffer, offset, length)
    offset = alignToFourBytes(offset + length * ArrayType.BYTES_PER_ELEMENT)
    return array
  }

  const { nodeCount, detailCount } = summary
  return {
    summary,
    parentIndex: nextArray(Int32Array, nodeCount),
    move: nextArray(Uint32Array, nodeCount),
    pushCount: nextArray(Uint32Array, nodeCount),
    heuristic: nextArray(Uint32Array, nodeCount),
    expansionOrder: nextArray(Uint32Array, nodeCount),
    stepMicroseconds: nextArray(Uint32Array, nodeCount),
    fate: nextArray(Uint8Array, nodeCount),
    dropReason: nextArray(Uint8Array, nodeCount),
    rejectedCounts: nextArray(Uint8Array, nodeCount * 8),
    detailParent: nextArray(Int32Array, detailCount),
    detailMove: nextArray(Uint32Array, detailCount),
    detailReason: nextArray(Uint8Array, detailCount),
  }
}
