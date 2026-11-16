<script lang="ts">
  import { formatCount, stateDetails, type SearchTree } from '@sokobot/core'
  import { onMount } from 'svelte'
  import { isTyping } from '../utils.ts'
  import { createOverviewMap, type OverviewMap } from '../canvas/overview/overview-map.ts'

  interface Props {
    tree: SearchTree | null
    selected: number | null
    empty: string
    onpick: (index: number) => void
  }

  const ZOOM_STEP = 1.6
  const ZOOM_KEYS: Record<string, number> = {
    '+': ZOOM_STEP,
    '=': ZOOM_STEP,
    '-': 1 / ZOOM_STEP,
    _: 1 / ZOOM_STEP,
  }

  let { tree, selected, empty, onpick }: Props = $props()
  let canvas: HTMLCanvasElement
  let navigator: HTMLCanvasElement
  let map: OverviewMap | undefined
  let isHovered = false
  let zoom = $state(1)
  let maxZoom = $state(1)
  let depth = $state<number | null>(null)
  let hovered = $state<number | null>(null)
  let isTall = $state(false)

  const hoverText = $derived.by(() => {
    if (!tree || hovered === null) return ''

    const d = stateDetails(tree, hovered)
    return `state #${formatCount(hovered)} · depth ${d.depth} · h ${d.heuristic} · ${d.fate}`
  })
  // the slider is logarithmic so each step feels like the same amount of zoom
  const sliderValue = $derived(maxZoom > 1 ? (Math.log(zoom) / Math.log(maxZoom)) * 100 : 0)

  onMount(() => {
    map = createOverviewMap(canvas, navigator, {
      onPick: (index) => onpick(index),
      onZoomChange: (next, max) => {
        zoom = next
        maxZoom = max
      },
      onCursor: (position) => (depth = position?.depth ?? null),
      onHover: (index) => (hovered = index),
    })
  })

  $effect(() => map?.setTree(tree))
  $effect(() => {
    void tree
    map?.select(selected)
  })

  const zoomIn = () => map?.zoomBy(ZOOM_STEP)
  const zoomOut = () => map?.zoomBy(1 / ZOOM_STEP)

  function onkeydown(event: KeyboardEvent) {
    if (!tree || isTyping(event) || event.altKey) return

    // ctrl/meta zoom keys belong to the browser unless the pointer is on the map
    if ((event.ctrlKey || event.metaKey) && !isHovered) return

    const factor = ZOOM_KEYS[event.key]
    if (factor) map?.zoomBy(factor)
    else if (event.key === '0') map?.fit()
    else if (event.key === 'f' && !event.ctrlKey && !event.metaKey) map?.focus()
    else return

    event.preventDefault()
  }
</script>

<svelte:window {onkeydown} />

<div
  class="relative"
  role="presentation"
  onpointerenter={() => (isHovered = true)}
  onpointerleave={() => (isHovered = false)}
>
  <canvas
    bind:this={canvas}
    class={[
      'box block min-h-80 w-full touch-none bg-map',
      isTall ? 'h-[80vh]' : 'h-[50vh] max-h-130',
      hovered === null ? 'cursor-grab active:cursor-grabbing' : 'cursor-pointer',
    ]}
  ></canvas>
  {#if !tree}
    <p
      class={[
        'pointer-events-none absolute inset-0 flex items-center justify-center',
        'p-4 text-center text-dim',
      ]}
    >
      {empty}
    </p>
  {/if}
  <div
    class="box absolute bottom-2.5 left-2.5 flex max-w-[calc(100%-180px)] flex-wrap items-center gap-1 bg-bg/90 px-1.5 py-0.5"
  >
    <button class="btn" title="Zoom out (−)" onclick={zoomOut}> − </button>
    <input
      class="w-24 accent-accent"
      type="range"
      min="0"
      max="100"
      step="0.5"
      title="Zoom"
      aria-label="Zoom"
      disabled={maxZoom <= 1}
      value={sliderValue}
      oninput={(event) => map?.zoomTo(maxZoom ** (Number(event.currentTarget.value) / 100))}
    />
    <button class="btn" title="Zoom in (+)" onclick={zoomIn}>+</button>
    <label class="flex items-center gap-1">
      <input
        class="field h-6 w-15 text-right"
        type="number"
        min="100"
        step="10"
        value={Math.round(zoom * 100)}
        onchange={(event) =>
          map?.zoomTo(Math.max(100, Number(event.currentTarget.value) || 100) / 100)}
      />
      %
    </label>
    <button class="btn" title="Fit on screen (0)" onclick={() => map?.fit()}>Fit</button>
    <button
      class="btn"
      title="Zoom in on the selected state (F)"
      disabled={selected === null}
      onclick={() => map?.focus()}
    >
      Selected
    </button>
    <button class="btn" title="Make the map taller or shorter" onclick={() => (isTall = !isTall)}>
      {isTall ? 'Shrink' : 'Expand'}
    </button>
  </div>
  <div
    class="pointer-events-none absolute top-2.5 left-2.5 flex gap-3 rounded-sm bg-bg/90 px-1.5 py-0.5 text-xs text-dim tabular-nums empty:hidden"
  >
    {#if hoverText}<span class="text-fg">{hoverText}</span>{/if}
    {#if depth !== null}<span>depth {formatCount(depth)}</span>{/if}
  </div>
  <canvas
    bind:this={navigator}
    title="Navigator: drag to move around"
    class="box absolute right-2.5 bottom-2.5 h-24.5 w-35 cursor-pointer touch-none bg-map"
  ></canvas>
</div>
