<script lang="ts">
  import {
    formatCount,
    formatDuration,
    childrenOf,
    neighbours,
    routeTo,
    PRUNE_LABELS,
    rejectedPushesOf,
    sessionAtNode,
    stateDetails,
    type Push,
    type Puzzle,
    type Neighbours,
    type SearchTree,
    type StateDetails,
  } from '@sokobot/core'
  import type { PushMarker } from '../canvas/board-painter.ts'
  import BoardCanvas from './BoardCanvas.svelte'
  import { readCssColor } from '../canvas/theme.ts'

  const BOARD_TILE_SIZE_PIXELS = 34
  const MAX_LISTED = 200
  const BREADCRUMB_HEAD = 2
  const BREADCRUMB_TAIL = 10

  const FATE_TEXT: Record<StateDetails['fate'], string> = {
    expanded: 'expanded',
    open: 'queued, never expanded',
    solution: 'the solution',
    dropped: 'dropped when popped',
  }
  const FATE_COLOR: Record<StateDetails['fate'], string> = {
    expanded: 'bg-expanded',
    open: 'bg-open',
    solution: 'bg-solution',
    dropped: 'bg-dropped',
  }
  const NAV_BUTTONS: [string, keyof Neighbours][] = [
    ['Start', 'root'],
    ['↑ Parent', 'parent'],
    ['← Prev', 'previous'],
    ['Next →', 'next'],
    ['↓ Child', 'child'],
    ['Solution', 'solution'],
    ['Dead end', 'deadEnd'],
  ]

  interface Props {
    tree: SearchTree
    puzzle: Puzzle
    selected: number | null
    rejectedIndex: number | null
    subtreeSizes: Uint32Array | null
    onselect: (index: number, rejectedIndex?: number) => void
  }

  let { tree, puzzle, selected, rejectedIndex, subtreeSizes, onselect }: Props = $props()

  const describePush = (push: Push) => `(${push.from.join(', ')}) → (${push.to.join(', ')})`

  const index = $derived(selected ?? 0)
  const details = $derived(stateDetails(tree, index))
  const rejected = $derived(rejectedPushesOf(tree, index))
  const session = $derived(sessionAtNode(puzzle, tree, index))
  const go = $derived(neighbours(tree, index))
  const children = $derived(childrenOf(tree, index))
  const rejectedTotal = $derived(
    [...details.rejectedByReason.values()].reduce((sum, count) => sum + count, 0),
  )

  const marker = $derived.by((): PushMarker | undefined => {
    const shown = rejectedIndex === null ? undefined : rejected[rejectedIndex]
    if (shown) return { push: shown.push, color: readCssColor('--color-dropped') }

    return details.push
      ? { push: details.push, color: readCssColor('--color-expanded') }
      : undefined
  })

  const crumbs = $derived.by(() => {
    const route = routeTo(tree, index)
    return route.length > BREADCRUMB_HEAD + BREADCRUMB_TAIL + 1
      ? [...route.slice(0, BREADCRUMB_HEAD), -1, ...route.slice(-BREADCRUMB_TAIL)]
      : route
  })

  const properties = $derived.by((): [string, string][] => {
    const d = details
    const fate =
      d.fate === 'expanded'
        ? `expanded (#${formatCount(d.expansionOrder)} off the queue)`
        : FATE_TEXT[d.fate]
    const dropped = d.dropReason ? `: ${PRUNE_LABELS[d.dropReason]}` : ''
    const rejectedText = [...d.rejectedByReason]
      .map(([reason, count]) => `${PRUNE_LABELS[reason]} ${count}`)
      .join(', ')

    return [
      ['Depth', String(d.depth)],
      ['Pushes so far', String(d.pushCount)],
      ['Heuristic', String(d.heuristic)],
      ...(d.expansionOrder > 0
        ? [['Step time', formatDuration(d.stepMicroseconds * 1_000)] as [string, string]]
        : []),
      ['Last push', d.push ? describePush(d.push) : 'the starting position'],
      ...(d.push
        ? [
            [
              'Box switch',
              `${d.switched ? 'yes, penalty applied' : 'no, same box'} · ${d.switchesSoFar} on this path`,
            ] as [string, string],
          ]
        : []),
      ['Fate', `${fate}${dropped}`],
      ['Rejected', rejectedText || 'none'],
    ]
  })
</script>

<div class="sections gap-4 [&>section+section]:pt-3.5">
  <section>
    <h2 class="heading">State {index === 0 ? '(start)' : `#${formatCount(index)}`}</h2>
    <div class="mb-2 flex flex-wrap gap-0.75">
      {#each NAV_BUTTONS as [label, key] (key)}
        <button
          class="btn btn-outline h-6 px-1.75 text-xs"
          disabled={go[key] === null}
          onclick={() => go[key] !== null && onselect(go[key])}
        >
          {label}
        </button>
      {/each}
    </div>
    <div class="box mb-2 flex items-center justify-center bg-black p-1.5">
      <BoardCanvas
        {puzzle}
        {session}
        tileSize={BOARD_TILE_SIZE_PIXELS}
        {marker}
        class="max-w-full"
      />
    </div>
    <div class="my-2">
      {#each properties as [label, value] (label)}
        <div class="flex gap-2 rounded-sm px-1 py-0.75 hover:bg-hover">
          <span class="w-24 flex-none text-dim">{label}</span>
          {value}
        </div>
      {/each}
    </div>
    <div class="flex flex-wrap items-center gap-0.5 text-[11px] text-dim">
      {#each crumbs as step, position (position)}
        {#if step < 0}
          <span> … </span>
        {:else}
          <button
            class={['btn btn-outline h-5.5 px-1.25 text-[11px]', step === index && 'bg-hover']}
            onclick={() => onselect(step)}
          >
            {step === 0 ? 'start' : `#${formatCount(step)}`}
          </button>
        {/if}
      {/each}
    </div>
  </section>

  <section>
    <h2 class="heading flex">
      Pushes that became states ({formatCount(children.length)})
      <span class="ml-auto font-normal">states below</span>
    </h2>
    <ul class="max-h-65 overflow-y-auto">
      {#each children.slice(0, MAX_LISTED) as child (child)}
        {@const child_ = stateDetails(tree, child)}
        {@const size = formatCount(subtreeSizes?.[child] ?? 1)}
        <li>
          <button
            class="row-button flex w-full items-center gap-1.5 px-1.5 py-1"
            title="{FATE_TEXT[child_.fate]} · {size} in its subtree"
            onclick={() => onselect(child)}
          >
            <i class={['size-1.5 flex-none', FATE_COLOR[child_.fate]]}></i>
            {child_.push ? describePush(child_.push) : 'start'}
            <span class="ml-auto text-dim tabular-nums">{size}</span>
          </button>
        </li>
      {/each}
      {#if children.length > MAX_LISTED}
        <li class="px-1.5 py-1 text-xs text-dim">
          + {formatCount(children.length - MAX_LISTED)} more
        </li>
      {:else if children.length === 0}
        <li class="px-1.5 py-1 text-xs text-dim">No pushes from here survived: a dead end.</li>
      {/if}
    </ul>
  </section>

  {#if rejectedTotal > 0}
    <section>
      <h2 class="heading">Pushes that were rejected ({formatCount(rejectedTotal)})</h2>
      <ul class="max-h-65 overflow-y-auto">
        {#each rejected.slice(0, MAX_LISTED) as entry, position (position)}
          <li>
            <button
              class={[
                'row-button flex w-full items-center gap-1.5 px-1.5 py-1',
                position === rejectedIndex && 'bg-hover',
              ]}
              onclick={() => onselect(index, position)}
            >
              <i class="size-1.5 flex-none bg-dropped"></i>
              {PRUNE_LABELS[entry.reason]} · {describePush(entry.push)}
            </button>
          </li>
        {/each}
        {#if rejected.length === 0}
          <li class="px-1.5 py-1 text-xs text-dim">
            Individual pushes are only kept for the first part of a very large tree.
          </li>
        {/if}
      </ul>
    </section>
  {/if}
</div>
