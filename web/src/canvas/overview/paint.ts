import {
  FATE_DROPPED,
  FATE_EXPANDED,
  FATE_OPEN,
  FATE_SOLUTION,
  type SearchTree,
} from '@sokobot/core'
import { packColor, parseHexColor, readCssColor } from '../theme.ts'
import { clamp, type View } from './view.ts'

const VECTOR_NODE_LIMIT = 50_000
const MIN_DOT_PIXELS = 2
const MAX_DOT_PIXELS = 8
const DOT_SCALE_FRACTION = 0.35
const VISIBLE_MARGIN_PIXELS = 8
const FATES_BY_PRIORITY = [FATE_OPEN, FATE_EXPANDED, FATE_DROPPED, FATE_SOLUTION]
const MAX_DENSITY = 255
const DENSITY_BASE_STRENGTH = 0.45
const DENSITY_LOG_DIVISOR = 9

const FATE_PRIORITY: Record<number, number> = {
  [FATE_OPEN]: 1,
  [FATE_EXPANDED]: 2,
  [FATE_DROPPED]: 3,
  [FATE_SOLUTION]: 4,
}

const FATE_VARIABLES: Record<number, string> = {
  [FATE_OPEN]: '--color-open',
  [FATE_EXPANDED]: '--color-expanded',
  [FATE_DROPPED]: '--color-dropped',
  [FATE_SOLUTION]: '--color-solution',
}

// one packed color per (fate, density) so the per-pixel loop is a table lookup
function densityPalette(): Uint32Array[] {
  const background = parseHexColor(readCssColor('--color-map'))
  const palette: Uint32Array[] = []

  for (const [fateValue, name] of Object.entries(FATE_VARIABLES)) {
    const color = parseHexColor(readCssColor(name))
    const table = new Uint32Array(MAX_DENSITY + 1)
    for (let count = 1; count <= MAX_DENSITY; count++) {
      const strength = Math.min(
        1,
        DENSITY_BASE_STRENGTH + Math.log2(1 + count) / DENSITY_LOG_DIVISOR,
      )
      const mix = (channel: number) =>
        Math.round(background[channel]! + (color[channel]! - background[channel]!) * strength)
      table[count] = packColor(mix(0), mix(1), mix(2))
    }
    palette[Number(fateValue)] = table
  }

  return palette
}

export function paintDensity(searchTree: SearchTree, width: number, height: number, view: View) {
  const pixelCount = width * height
  const density = new Uint8Array(pixelCount)
  const owners = new Int32Array(pixelCount).fill(-1)
  const { fate } = searchTree.exploration
  const { depth, row } = searchTree

  for (let index = 0; index < searchTree.nodeCount; index++) {
    const x = (view.offsetX + (depth[index]! + 0.5) * view.scaleX) | 0
    const y = (view.offsetY + (row[index]! + 0.5) * view.scaleY) | 0

    if (x < 0 || y < 0 || x >= width || y >= height) continue

    const pixel = y * width + x
    if (density[pixel]! < MAX_DENSITY) density[pixel]! += 1

    const owner = owners[pixel]!
    if (owner >= 0 && FATE_PRIORITY[fate[index]!]! < FATE_PRIORITY[fate[owner]!]!) continue

    owners[pixel] = index
  }

  const background = parseHexColor(readCssColor('--color-map'))
  const empty = packColor(background[0]!, background[1]!, background[2]!)
  const palette = densityPalette()
  const image = new ImageData(width, height)
  const pixels = new Uint32Array(image.data.buffer)

  for (let pixel = 0; pixel < pixelCount; pixel++) {
    const owner = owners[pixel]!
    pixels[pixel] = owner < 0 ? empty : palette[fate[owner]!]![density[pixel]!]!
  }

  return { image, owners }
}

export function collectVisible(
  searchTree: SearchTree,
  width: number,
  height: number,
  view: View,
): Int32Array | null {
  const found = new Int32Array(VECTOR_NODE_LIMIT)
  let count = 0
  for (let index = 0; index < searchTree.nodeCount; index++) {
    const x = view.offsetX + (searchTree.depth[index]! + 0.5) * view.scaleX
    const y = view.offsetY + (searchTree.row[index]! + 0.5) * view.scaleY
    const isVisible =
      x >= -VISIBLE_MARGIN_PIXELS &&
      y >= -VISIBLE_MARGIN_PIXELS &&
      x <= width + VISIBLE_MARGIN_PIXELS &&
      y <= height + VISIBLE_MARGIN_PIXELS

    if (!isVisible) continue

    if (count === VECTOR_NODE_LIMIT) return null

    found[count++] = index
  }

  return found.subarray(0, count)
}

export function paintVector(
  searchTree: SearchTree,
  width: number,
  height: number,
  view: View,
  visible: Int32Array,
  target: CanvasRenderingContext2D,
  pixelScale = window.devicePixelRatio || 1,
) {
  const owners = new Int32Array(width * height).fill(-1)
  const { parentIndex, fate } = searchTree.exploration
  const pixelX = (index: number) => view.offsetX + (searchTree.depth[index]! + 0.5) * view.scaleX
  const pixelY = (index: number) => view.offsetY + (searchTree.row[index]! + 0.5) * view.scaleY

  target.clearRect(0, 0, width, height)
  target.beginPath()
  for (const index of visible) {
    const parent = parentIndex[index]!
    if (parent < 0) continue

    target.moveTo(pixelX(parent), pixelY(parent))
    target.lineTo(pixelX(index), pixelY(index))
  }
  target.strokeStyle = readCssColor('--color-edge')
  target.lineWidth = pixelScale
  target.stroke()

  const size = Math.round(
    clamp(
      DOT_SCALE_FRACTION * Math.sqrt(view.scaleX * view.scaleY),
      MIN_DOT_PIXELS * pixelScale,
      MAX_DOT_PIXELS * pixelScale,
    ),
  )
  for (const fateValue of FATES_BY_PRIORITY) {
    target.fillStyle = readCssColor(FATE_VARIABLES[fateValue] ?? '')
    for (const index of visible) {
      if (fate[index]! !== fateValue) continue

      const left = Math.round(pixelX(index) - size / 2)
      const top = Math.round(pixelY(index) - size / 2)
      target.fillRect(left, top, size, size)

      for (let y = Math.max(0, top); y < Math.min(height, top + size); y++) {
        for (let x = Math.max(0, left); x < Math.min(width, left + size); x++) {
          owners[y * width + x] = index
        }
      }
    }
  }

  return owners
}
