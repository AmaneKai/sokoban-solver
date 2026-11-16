<script lang="ts">
  import { placedBoxCount, type Direction } from '@sokobot/core'
  import { onMount } from 'svelte'
  import BoardCanvas from '../components/BoardCanvas.svelte'
  import BuildButton from '../components/BuildButton.svelte'
  import { isTyping, normalizeKey } from '../utils.ts'
  import { solver } from '../stores/solver.svelte.ts'

  const DIRECTION_KEYS: Record<string, Direction> = {
    ArrowUp: 'u',
    w: 'u',
    ArrowDown: 'd',
    s: 'd',
    ArrowLeft: 'l',
    a: 'l',
    ArrowRight: 'r',
    d: 'r',
  }
  const MIN_TILE_PX = 12
  const MAX_TILE_PX = 48
  const STAGE_PADDING_PX = 16
  const RESERVED_HEIGHT_PX = 140

  let stageWidth = $state(0)
  let windowHeight = $state(0)

  const { puzzle, session } = $derived(solver)
  const tileSize = $derived.by(() => {
    if (!puzzle) return MIN_TILE_PX
    const fits = Math.min(
      Math.floor((stageWidth - STAGE_PADDING_PX) / puzzle.columnCount),
      Math.floor((windowHeight - RESERVED_HEIGHT_PX) / puzzle.rowCount),
    )

    return Math.max(MIN_TILE_PX, Math.min(MAX_TILE_PX, fits))
  })
  const boxes = $derived(
    puzzle && session ? `${placedBoxCount(puzzle, session)} / ${session.boxes.size}` : '0 / 0',
  )

  function onkeydown(event: KeyboardEvent) {
    if (isTyping(event) || event.ctrlKey || event.metaKey || event.altKey) return

    const key = normalizeKey(event)
    if (key === 'z') solver.undo()
    if (key === 'r') solver.reset()

    const direction = DIRECTION_KEYS[key]
    if (!direction) return

    event.preventDefault()
    if (!solver.playing) solver.move(direction)
  }

  onMount(() => {
    if (!solver.puzzle) void solver.init()
  })
</script>

<svelte:window {onkeydown} bind:innerHeight={windowHeight} />

<div class="flex min-h-screen flex-col">
  <header class="page-header">
    <h1 class="mr-3 text-sm font-semibold">SokoBot</h1>
    <select
      class="field max-w-85 cursor-pointer border-transparent hover:bg-hover"
      aria-label="Map"
      value={solver.mapName}
      onchange={(event) => void solver.selectMap(event.currentTarget.value)}
    >
      {#each solver.maps as name (name)}
        <option>{name}</option>
      {/each}
    </select>
    <button class="btn" title="Previous map" onclick={() => solver.neighbourMap(-1)}>‹</button>
    <button class="btn" title="Next map" onclick={() => solver.neighbourMap(1)}>›</button>
    <button class="btn" title="Reload map (R)" onclick={() => solver.reset()}>Reset</button>
    <span class="flex-1"></span>
    <BuildButton onnotice={(next) => (solver.notice = next)} solver={solver.solverId} />
    {#if solver.solverInfo?.explorable}
      <a
        class="btn inline-flex items-center"
        title="Search tree of the last winning thread"
        href="#/explore?map={encodeURIComponent(solver.mapName)}&solver={encodeURIComponent(solver.solverId)}&thread={solver.winnerThread ??
          0}&autorun=1"
      >
        Explore
      </a>
    {:else}
      <span
        class="btn inline-flex cursor-not-allowed items-center opacity-40"
        title="{solver.solverInfo?.name ?? 'This solver'} has no explorer"
      >
        Explore
      </span>
    {/if}
  </header>

  <main class="flex flex-1 flex-col gap-6 p-4 md:flex-row">
    <div
      bind:clientWidth={stageWidth}
      class="flex min-h-75 flex-1 items-center justify-center overflow-auto rounded-sm bg-black"
    >
      {#if puzzle && session}
        <BoardCanvas {puzzle} {session} {tileSize} class="max-w-full" />
      {/if}
    </div>

    <aside class={['sections w-full md:w-70', '[&>section]:py-3 [&>section:first-child]:pt-0']}>
      <section>
        <h2 class="heading">Play</h2>
        <p class="mb-2 text-dim">
          Arrow keys / WASD to move, <kbd>Z</kbd> to undo, <kbd>R</kbd> to reset.
        </p>
        <dl class="mb-2 grid grid-cols-[90px_1fr] tabular-nums [&>*]:py-1">
          <dt class="text-dim">Moves</dt>
          <dd>{session?.history.length ?? 0}</dd>
          <dt class="text-dim">Boxes</dt>
          <dd>{boxes}</dd>
        </dl>
        <button class="btn btn-outline" onclick={() => solver.undo()}>Undo</button>
      </section>

      <section>
        <h2 class="heading">Bot</h2>
        {#if solver.solvers.length > 1}
          <label class="label">
            Solver
            <select class="field w-full text-fg" bind:value={solver.solverId}>
              {#each solver.solvers as info (info.id)}
                <option value={info.id}>{info.name}{info.built ? '' : ' (not built)'}</option>
              {/each}
            </select>
          </label>
        {/if}
        {#if solver.solverInfo?.threads}
          <label class="label">
            Thread
            <select class="field w-full text-fg" bind:value={solver.thread}>
              <option value={-1}>Auto (all)</option>
              {#each [0, 1, 2, 3, 4] as id (id)}
                <option value={id}>Thread {id}</option>
              {/each}
            </select>
          </label>
        {/if}
        <label class="label">
          Timeout (s, 0 = none)
          <input
            class="field w-full text-fg"
            type="number"
            min="0"
            bind:value={solver.timeoutSeconds}
          />
        </label>
        <div class="mb-2 flex gap-1 [&>button]:flex-1">
          <button
            class="btn btn-primary"
            disabled={solver.solving || !puzzle}
            onclick={() => void solver.solve()}
          >
            Solve
          </button>
          <button
            class="btn btn-outline"
            disabled={!solver.solving}
            onclick={() => void solver.cancel()}
          >
            Cancel
          </button>
        </div>
        <p class="mb-2" data-tone={solver.notice.tone}>{solver.notice.text}</p>

        {#if solver.solution}
          <div class="mb-2 flex gap-1 [&>button]:flex-1">
            <button class="btn btn-outline" onclick={() => solver.togglePlay()}>
              {solver.playing ? 'Pause' : 'Play'}
            </button>
            <button
              class="btn btn-outline"
              onclick={() => {
                solver.pause()
                solver.step()
              }}
            >
              Step
            </button>
          </div>
          <label class="label">
            Speed
            <input
              class="w-full accent-accent"
              type="range"
              min="0"
              max="10"
              bind:value={solver.speed}
            />
          </label>
          <p class="text-dim">
            {solver.played === 0 ? '' : `${solver.played} / `}{solver.solution.length} moves
          </p>
        {/if}
      </section>
    </aside>
  </main>
</div>
