import { positionKey, type Push, type Position, type Puzzle, type Session } from '@sokobot/core'
import brick from './graphics/brick.png?inline'
import crate from './graphics/crate.png?inline'
import crategoal from './graphics/crategoal.png?inline'
import goal from './graphics/goal.png?inline'
import robot from './graphics/robot.png?inline'

const SPRITE_URLS = { brick, goal, crate, crategoal, robot }
const DASH_PATTERN = [4, 3]

type Sprites = Record<keyof typeof SPRITE_URLS, HTMLImageElement>

export interface PushMarker {
  push: Push
  color: string
}

export function loadSprites(onLoaded: () => void): Sprites {
  return Object.fromEntries(
    Object.entries(SPRITE_URLS).map(([name, url]) => {
      const image = new Image()
      image.onload = onLoaded
      image.src = url

      return [name, image]
    }),
  ) as Sprites
}

function drawSprite(
  drawing: CanvasRenderingContext2D,
  sprite: HTMLImageElement,
  [row, column]: Position,
  tileSize: number,
) {
  drawing.drawImage(sprite, column * tileSize, row * tileSize, tileSize, tileSize)
}

const cellCenter = ([row, column]: Position, tileSize: number): [number, number] => [
  (column + 0.5) * tileSize,
  (row + 0.5) * tileSize,
]

function drawPushMarker(
  drawing: CanvasRenderingContext2D,
  { push, color }: PushMarker,
  tileSize: number,
) {
  const [fromX, fromY] = cellCenter(push.from, tileSize)
  const [toX, toY] = cellCenter(push.to, tileSize)
  const inset = tileSize / 2 - 1
  drawing.strokeStyle = color
  drawing.lineWidth = Math.max(2, tileSize / 10)
  drawing.setLineDash(DASH_PATTERN)
  drawing.strokeRect(fromX - inset, fromY - inset, inset * 2, inset * 2)
  drawing.setLineDash([])
  drawing.strokeRect(toX - inset, toY - inset, inset * 2, inset * 2)
  drawing.beginPath()
  drawing.moveTo(fromX, fromY)
  drawing.lineTo(toX, toY)
  drawing.stroke()
}

export function paintBoard(
  target: HTMLCanvasElement,
  sprites: Sprites,
  puzzle: Puzzle,
  session: Session,
  tileSize: number,
  marker?: PushMarker,
) {
  const drawing = target.getContext('2d')
  if (!drawing) return

  const width = puzzle.columnCount * tileSize
  const height = puzzle.rowCount * tileSize
  if (target.width !== width || target.height !== height) {
    target.width = width
    target.height = height
  } else {
    drawing.clearRect(0, 0, width, height)
  }
  drawing.imageSmoothingEnabled = false

  for (let row = 0; row < puzzle.rowCount; row++) {
    for (let column = 0; column < puzzle.columnCount; column++) {
      const position: Position = [row, column]
      const key = positionKey(position)
      const isGoal = puzzle.goals.has(key)

      if (puzzle.walls.has(key)) drawSprite(drawing, sprites.brick, position, tileSize)
      else if (isGoal) drawSprite(drawing, sprites.goal, position, tileSize)

      if (session.boxes.has(key)) {
        drawSprite(drawing, isGoal ? sprites.crategoal : sprites.crate, position, tileSize)
      }
    }
  }

  drawSprite(drawing, sprites.robot, session.player, tileSize)
  if (marker) drawPushMarker(drawing, marker, tileSize)
}
