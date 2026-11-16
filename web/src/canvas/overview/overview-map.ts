import { routeTo, type SearchTree } from '@sokobot/core'
import { pointer, select } from 'd3-selection'
import { zoom as d3Zoom, zoomIdentity } from 'd3-zoom'
import { readCssColor } from '../theme.ts'
import { collectVisible, paintDensity, paintVector } from './paint.ts'
import { clamp, fitView, viewOf } from './view.ts'

const PICK_RADIUS_PIXELS = 8
const ROUTE_COLOR = '#ffffff'

const MIN_VISIBLE_ROWS = 16
const MAX_ZOOM = 4096
const MIN_VISIBLE_DEPTHS = 8
const FOLLOW_MARGIN_PIXELS = 40
const FOCUS_ZOOM = 8
const HOVER_COLOR = '#ffffffa0'
// d3's default wheel step is far too coarse for trackpads on deep trees, so scale it down
const WHEEL_PIXEL_STEP = 0.0012
const WHEEL_LINE_STEP = 0.03
const PINCH_MULTIPLIER = 5
const MAX_STEP_FACTOR = 1.25

interface OverviewCallbacks {
  onPick: (index: number) => void
  onZoomChange: (zoom: number, maxZoom: number) => void
  onCursor: (position: { depth: number } | null) => void
  onHover: (index: number | null) => void
}

export interface OverviewMap {
  setTree(tree: SearchTree | null): void
  select(index: number | null): void
  zoomBy(factor: number): void
  zoomTo(zoom: number): void
  focus(): void
  fit(): void
}

function requireContext(target: HTMLCanvasElement): CanvasRenderingContext2D {
  const context = target.getContext('2d')
  if (!context) throw new Error('Canvas 2D context is not available')

  return context
}

function sizeToLayout(target: HTMLCanvasElement): boolean {
  const ratio = window.devicePixelRatio || 1
  const width = Math.max(1, Math.round(target.clientWidth * ratio))
  const height = Math.max(1, Math.round(target.clientHeight * ratio))
  if (target.width === width && target.height === height) return false

  target.width = width
  target.height = height
  return true
}

export function createOverviewMap(
  canvas: HTMLCanvasElement,
  navigator: HTMLCanvasElement,
  callbacks: OverviewCallbacks,
): OverviewMap {
  const { onPick, onZoomChange, onCursor, onHover } = callbacks
  const context = requireContext(canvas)
  const navigatorContext = requireContext(navigator)
  const pictureCanvas = document.createElement('canvas')
  const pictureContext = requireContext(pictureCanvas)
  const thumbnailCanvas = document.createElement('canvas')
  const thumbnailContext = requireContext(thumbnailCanvas)
  let tree: SearchTree | null = null
  let selectedIndex: number | null = null
  let hoveredIndex: number | null = null
  let hasPicture = false
  let owners = new Int32Array(0)
  let zoomX = 1
  let zoomY = 1
  let centerDepth = 0
  let centerRow = 0
  let dragOffsetX = 0
  let dragOffsetY = 0
  let isPictureStale = false
  let frameRequest = 0

  const pixelRatio = () => window.devicePixelRatio || 1
  const currentView = (searchTree: SearchTree) =>
    viewOf(searchTree, canvas.width, canvas.height, zoomX, zoomY, centerDepth, centerRow)
  const isZoomed = () => zoomX > 1 || zoomY > 1
  const maxZoomY = (searchTree: SearchTree) =>
    Math.min(MAX_ZOOM, Math.max(2, searchTree.rowCount / MIN_VISIBLE_ROWS))
  const maxZoomX = (searchTree: SearchTree) =>
    Math.max(1, (searchTree.maxDepth + 1) / MIN_VISIBLE_DEPTHS)
  const couplingExponent = (searchTree: SearchTree) =>
    maxZoomX(searchTree) > 1
      ? clamp(Math.log(maxZoomX(searchTree)) / Math.log(maxZoomY(searchTree)), 0, 1)
      : 0

  function pixelOf(searchTree: SearchTree, index: number) {
    const view = currentView(searchTree)
    return {
      x: view.offsetX + (searchTree.depth[index]! + 0.5) * view.scaleX,
      y: view.offsetY + (searchTree.row[index]! + 0.5) * view.scaleY,
    }
  }

  function clampCenter(searchTree: SearchTree) {
    const halfDepth = (searchTree.maxDepth + 1) / zoomX / 2
    const halfRows = searchTree.rowCount / zoomY / 2
    centerDepth = clamp(centerDepth, halfDepth - 0.5, searchTree.maxDepth + 0.5 - halfDepth)
    centerRow = clamp(centerRow, halfRows - 0.5, searchTree.rowCount - 0.5 - halfRows)
  }

  function renderPicture(searchTree: SearchTree) {
    if (pictureCanvas.width !== canvas.width) pictureCanvas.width = canvas.width
    if (pictureCanvas.height !== canvas.height) pictureCanvas.height = canvas.height
    const view = currentView(searchTree)
    const visible = collectVisible(searchTree, canvas.width, canvas.height, view)

    if (visible) {
      owners = paintVector(searchTree, canvas.width, canvas.height, view, visible, pictureContext)
    } else {
      const painted = paintDensity(searchTree, canvas.width, canvas.height, view)
      owners = painted.owners
      pictureContext.putImageData(painted.image, 0, 0)
    }
    hasPicture = true
  }

  function renderThumbnail(searchTree: SearchTree) {
    sizeToLayout(navigator)
    const painted = paintDensity(
      searchTree,
      navigator.width,
      navigator.height,
      fitView(searchTree, navigator.width, navigator.height),
    )
    if (thumbnailCanvas.width !== navigator.width) thumbnailCanvas.width = navigator.width
    if (thumbnailCanvas.height !== navigator.height) thumbnailCanvas.height = navigator.height
    thumbnailContext.putImageData(painted.image, 0, 0)
  }

  function strokeRoute(
    searchTree: SearchTree,
    indices: Iterable<number>,
    color: string,
    width: number,
  ) {
    context.beginPath()
    let isFirst = true
    for (const index of indices) {
      const { x, y } = pixelOf(searchTree, index)
      if (isFirst) context.moveTo(x, y)
      else context.lineTo(x, y)
      isFirst = false
    }
    context.strokeStyle = color
    context.lineWidth = width
    context.stroke()
  }

  function drawNavigator(searchTree: SearchTree) {
    const { width, height } = navigator
    navigatorContext.clearRect(0, 0, width, height)
    navigatorContext.drawImage(thumbnailCanvas, 0, 0)
    const thumbnail = fitView(searchTree, width, height)
    const view = currentView(searchTree)
    const halfDepth = canvas.width / 2 / view.scaleX
    const halfRows = canvas.height / 2 / view.scaleY
    const middleDepth = searchTree.maxDepth / 2
    const middleRow = (searchTree.rowCount - 1) / 2
    const left = width / 2 + (centerDepth - halfDepth - middleDepth) * thumbnail.scaleX
    const right = width / 2 + (centerDepth + halfDepth - middleDepth) * thumbnail.scaleX
    const top = height / 2 + (centerRow - halfRows - middleRow) * thumbnail.scaleY
    const bottom = height / 2 + (centerRow + halfRows - middleRow) * thumbnail.scaleY

    navigatorContext.strokeStyle = ROUTE_COLOR
    navigatorContext.lineWidth = pixelRatio()
    navigatorContext.strokeRect(left, top, right - left, bottom - top)
  }

  function paint() {
    context.setTransform(1, 0, 0, 1, 0, 0)
    context.fillStyle = readCssColor('--color-map')
    context.fillRect(0, 0, canvas.width, canvas.height)
    if (!tree) return

    const ratio = pixelRatio()
    context.setTransform(1, 0, 0, 1, dragOffsetX, dragOffsetY)

    if (hasPicture) context.drawImage(pictureCanvas, 0, 0)

    strokeRoute(tree, tree.solutionPath, readCssColor('--color-solution'), 2 * ratio)

    if (selectedIndex !== null) {
      strokeRoute(tree, routeTo(tree, selectedIndex), ROUTE_COLOR, 2 * ratio)
      const { x, y } = pixelOf(tree, selectedIndex)
      context.strokeStyle = ROUTE_COLOR
      context.lineWidth = 2 * ratio
      context.strokeRect(x - 6 * ratio, y - 6 * ratio, 12 * ratio, 12 * ratio)
    }

    if (hoveredIndex !== null && hoveredIndex !== selectedIndex) {
      const { x, y } = pixelOf(tree, hoveredIndex)
      context.strokeStyle = HOVER_COLOR
      context.lineWidth = ratio
      context.strokeRect(x - 5 * ratio, y - 5 * ratio, 10 * ratio, 10 * ratio)
    }

    context.setTransform(1, 0, 0, 1, 0, 0)
    drawNavigator(tree)
  }

  function draw() {
    if (frameRequest) return

    frameRequest = requestAnimationFrame(() => {
      frameRequest = 0
      if (tree && isPictureStale) renderPicture(tree)
      isPictureStale = false
      paint()
    })
  }

  function refresh() {
    if (!tree) return

    clampCenter(tree)
    isPictureStale = true
    draw()
    onZoomChange(zoomY, maxZoomY(tree))
  }

  function setZoom(nextX: number, nextY: number, anchorX: number, anchorY: number) {
    if (!tree) return

    const before = currentView(tree)
    const anchorDepth = (anchorX - before.offsetX) / before.scaleX - 0.5
    const anchorRow = (anchorY - before.offsetY) / before.scaleY - 0.5
    zoomX = nextX
    zoomY = nextY

    const after = currentView(tree)
    centerDepth = anchorDepth - (anchorX - canvas.width / 2) / after.scaleX
    centerRow = anchorRow - (anchorY - canvas.height / 2) / after.scaleY
    refresh()
  }

  function zoomAround(factor: number, anchorX: number, anchorY: number) {
    if (!tree) return

    const nextY = clamp(zoomY * factor, 1, maxZoomY(tree))
    const appliedFactor = nextY / zoomY
    const nextX = clamp(zoomX * appliedFactor ** couplingExponent(tree), 1, maxZoomX(tree))
    setZoom(nextX, nextY, anchorX, anchorY)
  }

  function zoomAnchor() {
    const fallback = { x: canvas.width / 2, y: canvas.height / 2 }
    if (!tree || selectedIndex === null) return fallback

    const anchor = pixelOf(tree, selectedIndex)
    const isOnScreen =
      anchor.x >= 0 && anchor.y >= 0 && anchor.x <= canvas.width && anchor.y <= canvas.height

    return isOnScreen ? anchor : fallback
  }

  function resetView(searchTree: SearchTree) {
    zoomX = 1
    zoomY = 1
    centerDepth = searchTree.maxDepth / 2
    centerRow = (searchTree.rowCount - 1) / 2
  }

  function commitPan() {
    if (!tree || (dragOffsetX === 0 && dragOffsetY === 0)) return

    const view = currentView(tree)
    centerDepth -= dragOffsetX / view.scaleX
    centerRow -= dragOffsetY / view.scaleY
    dragOffsetX = 0
    dragOffsetY = 0
    refresh()
  }

  function stateAt(pixelX: number, pixelY: number): number | null {
    const { width, height } = canvas
    const radius = Math.round(PICK_RADIUS_PIXELS * pixelRatio())
    let best: number | null = null
    let bestDistance = Infinity

    for (let y = Math.max(0, pixelY - radius); y <= Math.min(height - 1, pixelY + radius); y++) {
      for (let x = Math.max(0, pixelX - radius); x <= Math.min(width - 1, pixelX + radius); x++) {
        const owner = owners[y * width + x]!
        const distance = (x - pixelX) ** 2 + (y - pixelY) ** 2

        if (owner < 0 || distance >= bestDistance) continue

        best = owner
        bestDistance = distance
      }
    }

    return best
  }

  function pointerPixels(event: MouseEvent) {
    const bounds = canvas.getBoundingClientRect()
    return {
      x: Math.round(((event.clientX - bounds.left) / bounds.width) * canvas.width),
      y: Math.round(((event.clientY - bounds.top) / bounds.height) * canvas.height),
    }
  }

  function setHovered(index: number | null) {
    if (index === hoveredIndex) return

    hoveredIndex = index
    onHover(index)
    draw()
  }

  function reportCursor(x: number, y: number) {
    if (!tree) return

    const view = currentView(tree)
    const depth = Math.round((x - view.offsetX) / view.scaleX - 0.5)
    const row = Math.round((y - view.offsetY) / view.scaleY - 0.5)
    const isInside = depth >= 0 && depth <= tree.maxDepth && row >= 0 && row < tree.rowCount
    onCursor(isInside ? { depth } : null)
  }

  // d3-zoom only normalizes gestures (drag, wheel, pinch); only ratios between
  // consecutive events are used, the real zoom limits live in zoomAround
  let lastTransform = zoomIdentity
  const gestures = d3Zoom<HTMLCanvasElement, unknown>()
    .scaleExtent([1e-9, 1e9])
    .clickDistance(4)
    .wheelDelta(
      (event: WheelEvent) =>
        -event.deltaY *
        (event.deltaMode === 1 ? WHEEL_LINE_STEP : event.deltaMode ? 1 : WHEEL_PIXEL_STEP) *
        (event.ctrlKey ? PINCH_MULTIPLIER : 1),
    )
    .on('zoom', ({ transform, sourceEvent }) => {
      const ratio = clamp(transform.k / lastTransform.k, 1 / MAX_STEP_FACTOR, MAX_STEP_FACTOR)
      if (tree && sourceEvent) {
        if (ratio !== 1) {
          const [x, y] = pointer(sourceEvent, canvas)
          const toPixels = canvas.width / canvas.clientWidth
          zoomAround(ratio, x * toPixels, y * toPixels)
        } else if (isZoomed()) {
          dragOffsetX += (transform.x - lastTransform.x) * pixelRatio()
          dragOffsetY += (transform.y - lastTransform.y) * pixelRatio()
          draw()
        }
      }
      lastTransform = transform
    })
    .on('end', commitPan)

  select(canvas)
    .call(gestures)
    .on('click', (event: MouseEvent) => {
      const { x, y } = pointerPixels(event)
      const index = stateAt(x, y)
      if (index !== null) onPick(index)
    })
    .on('pointermove', (event: PointerEvent) => {
      const { x, y } = pointerPixels(event)
      reportCursor(x, y)
      if (event.buttons === 0) setHovered(stateAt(x, y))
    })
    .on('pointerleave', () => {
      onCursor(null)
      setHovered(null)
    })

  function centerFromNavigator(event: PointerEvent) {
    if (!tree) return

    const bounds = navigator.getBoundingClientRect()
    const x = ((event.clientX - bounds.left) / bounds.width) * navigator.width
    const y = ((event.clientY - bounds.top) / bounds.height) * navigator.height
    const view = fitView(tree, navigator.width, navigator.height)

    centerDepth = tree.maxDepth / 2 + (x - navigator.width / 2) / view.scaleX
    centerRow = (tree.rowCount - 1) / 2 + (y - navigator.height / 2) / view.scaleY
    refresh()
  }

  navigator.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) return

    navigator.setPointerCapture(event.pointerId)
    centerFromNavigator(event)
  })

  navigator.addEventListener('pointermove', (event) => {
    if (navigator.hasPointerCapture(event.pointerId) && event.buttons !== 0)
      centerFromNavigator(event)
  })

  new ResizeObserver(() => {
    if (!sizeToLayout(canvas) || !tree) return

    refresh()
  }).observe(canvas)

  new ResizeObserver(() => {
    if (!tree) return

    renderThumbnail(tree)
    draw()
  }).observe(navigator)

  return {
    setTree(newTree) {
      tree = newTree
      selectedIndex = null
      hoveredIndex = null
      hasPicture = false
      sizeToLayout(canvas)

      if (tree) {
        resetView(tree)
        renderThumbnail(tree)
        refresh()
      } else {
        draw()
      }
    },
    select(index) {
      selectedIndex = index
      if (!tree || index === null || !isZoomed()) return draw()

      const { x, y } = pixelOf(tree, index)
      const margin = FOLLOW_MARGIN_PIXELS * pixelRatio()
      const isVisible =
        x >= margin && y >= margin && x <= canvas.width - margin && y <= canvas.height - margin

      if (isVisible) return draw()

      centerDepth = tree.depth[index]!
      centerRow = tree.row[index]!
      refresh()
    },
    zoomBy(factor) {
      const anchor = zoomAnchor()
      zoomAround(factor, anchor.x, anchor.y)
    },
    zoomTo(nextZoom) {
      if (!tree) return

      const anchor = zoomAnchor()
      const nextY = clamp(nextZoom, 1, maxZoomY(tree))
      const nextX = clamp(nextY ** couplingExponent(tree), 1, maxZoomX(tree))
      setZoom(nextX, nextY, anchor.x, anchor.y)
    },
    focus() {
      if (!tree || selectedIndex === null) return

      zoomY = clamp(Math.max(zoomY, FOCUS_ZOOM), 1, maxZoomY(tree))
      zoomX = clamp(zoomY ** couplingExponent(tree), zoomX, maxZoomX(tree))
      centerDepth = tree.depth[selectedIndex]!
      centerRow = tree.row[selectedIndex]!
      refresh()
    },
    fit() {
      if (!tree) return

      resetView(tree)
      refresh()
    },
  }
}
