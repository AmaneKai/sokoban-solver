import type { SearchTree } from '@sokobot/core'

const PADDING_PIXELS = 10

export interface View {
  scaleX: number
  scaleY: number
  offsetX: number
  offsetY: number
}

export const clamp = (value: number, low: number, high: number) =>
  Math.min(Math.max(value, low), Math.max(low, high))

export function viewOf(
  searchTree: SearchTree,
  width: number,
  height: number,
  zoomX: number,
  zoomY: number,
  centerDepth: number,
  centerRow: number,
): View {
  const scaleX = ((width - PADDING_PIXELS * 2) / (searchTree.maxDepth + 1)) * zoomX
  const scaleY = ((height - PADDING_PIXELS * 2) / searchTree.rowCount) * zoomY

  return {
    scaleX,
    scaleY,
    offsetX: width / 2 - (centerDepth + 0.5) * scaleX,
    offsetY: height / 2 - (centerRow + 0.5) * scaleY,
  }
}

export const fitView = (searchTree: SearchTree, width: number, height: number): View =>
  viewOf(searchTree, width, height, 1, 1, searchTree.maxDepth / 2, (searchTree.rowCount - 1) / 2)
