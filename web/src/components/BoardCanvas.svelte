<script lang="ts">
  import type { Puzzle, Session } from '@sokobot/core'
  import { loadSprites, paintBoard, type PushMarker } from '../canvas/board-painter.ts'

  interface Props {
    puzzle: Puzzle
    session: Session
    tileSize: number
    marker?: PushMarker | undefined
    class?: string
  }

  let { puzzle, session, tileSize, marker, class: className = '' }: Props = $props()
  let canvas: HTMLCanvasElement
  let spritesLoaded = $state(0)
  const sprites = loadSprites(() => spritesLoaded++)

  $effect(() => {
    void spritesLoaded
    paintBoard(canvas, sprites, puzzle, session, tileSize, marker)
  })
</script>

<canvas bind:this={canvas} class={['[image-rendering:pixelated]', className]}></canvas>
