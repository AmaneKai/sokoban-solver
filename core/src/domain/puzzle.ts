export type Direction = 'u' | 'd' | 'l' | 'r'

export type Position = readonly [row: number, column: number]

interface HistoryEntry {
  readonly from: Position
  readonly pushedBox: boolean
}

export interface Session {
  readonly player: Position
  readonly boxes: ReadonlySet<string>
  readonly history: readonly HistoryEntry[]
}

export interface Puzzle {
  readonly text: string
  readonly rowCount: number
  readonly columnCount: number
  readonly walls: ReadonlySet<string>
  readonly goals: ReadonlySet<string>
  readonly start: Session
  readonly warning: string | null
}

const DIRECTION_DELTAS: Record<Direction, Position> = {
  u: [-1, 0],
  d: [1, 0],
  l: [0, -1],
  r: [0, 1],
}

export const positionKey = ([row, column]: Position) => `${row},${column}`

const step = ([row, column]: Position, direction: Direction): Position => {
  const [rowDelta, columnDelta] = DIRECTION_DELTAS[direction]

  return [row + rowDelta, column + columnDelta]
}

const findPositions = (lines: readonly string[], symbols: string): Position[] =>
  lines.flatMap((line, row) =>
    [...line].flatMap((symbol, column) =>
      symbols.includes(symbol) ? [[row, column] as const] : [],
    ),
  )

const trimTrailingBlankLines = (lines: string[]): string[] => {
  const lastContentIndex = lines.findLastIndex((line) => line.trim() !== '')

  return lines.slice(0, lastContentIndex + 1)
}

export function parsePuzzle(text: string): Puzzle {
  const lines = trimTrailingBlankLines(text.replace(/\r/g, '').split('\n'))
  const players = findPositions(lines, '@+')
  const boxes = findPositions(lines, '$*')
  const goals = findPositions(lines, '.+*')
  const isWellFormed = players.length === 1 && boxes.length > 0 && boxes.length === goals.length

  return {
    text,
    rowCount: lines.length,
    columnCount: Math.max(0, ...lines.map((line) => line.length)),
    walls: new Set(findPositions(lines, '#').map(positionKey)),
    goals: new Set(goals.map(positionKey)),
    start: {
      player: players[0] ?? [0, 0],
      boxes: new Set(boxes.map(positionKey)),
      history: [],
    },
    warning: isWellFormed
      ? null
      : `Map looks malformed (${players.length} players, ` +
        `${boxes.length} boxes, ${goals.length} goals).`,
  }
}

export const placedBoxCount = (puzzle: Puzzle, session: Session) =>
  [...session.boxes].filter((key) => puzzle.goals.has(key)).length

const isBlocked = (puzzle: Puzzle, position: Position): boolean => {
  const [row, column] = position
  const isOutside = row < 0 || row >= puzzle.rowCount || column < 0 || column >= puzzle.columnCount

  return isOutside || puzzle.walls.has(positionKey(position))
}

function relocateBox(boxes: ReadonlySet<string>, from: Position, to: Position): Set<string> {
  const relocated = new Set(boxes)
  relocated.delete(positionKey(from))
  relocated.add(positionKey(to))

  return relocated
}

function pushBox(
  puzzle: Puzzle,
  boxes: ReadonlySet<string>,
  from: Position,
  direction: Direction,
): ReadonlySet<string> | null {
  const destination = step(from, direction)
  if (isBlocked(puzzle, destination) || boxes.has(positionKey(destination))) return null

  return relocateBox(boxes, from, destination)
}

export function movePlayer(puzzle: Puzzle, session: Session, direction: Direction): Session | null {
  const target = step(session.player, direction)
  if (isBlocked(puzzle, target)) return null

  const pushesBox = session.boxes.has(positionKey(target))
  const boxes = pushesBox ? pushBox(puzzle, session.boxes, target, direction) : session.boxes
  if (!boxes) return null

  return {
    player: target,
    boxes,
    history: [...session.history, { from: session.player, pushedBox: pushesBox }],
  }
}

export function undoMove(session: Session): Session {
  const last = session.history.at(-1)
  if (!last) return session

  const [row, column] = session.player
  const boxAhead: Position = [2 * row - last.from[0], 2 * column - last.from[1]]
  const boxes = last.pushedBox
    ? relocateBox(session.boxes, boxAhead, session.player)
    : session.boxes

  return { player: last.from, boxes, history: session.history.slice(0, -1) }
}
