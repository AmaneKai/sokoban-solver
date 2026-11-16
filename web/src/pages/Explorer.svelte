<script lang="ts">
  import type { Neighbours } from '@sokobot/core'
  import { onMount } from 'svelte'
  import BuildButton from '../components/BuildButton.svelte'
  import { explorer } from '../stores/explorer.svelte.ts'
  import { isTyping, normalizeKey } from '../utils.ts'
  import Overview from '../components/Overview.svelte'
  import StatePanel from '../components/StatePanel.svelte'
  import Stats from '../components/Stats.svelte'
  import KeyMetrics from '../components/KeyMetrics.svelte'
  import Profile from '../components/Profile.svelte'

  const KEY_TARGETS: Record<string, keyof Neighbours> = {
    ArrowUp: 'parent',
    ArrowDown: 'child',
    ArrowLeft: 'previous',
    ArrowRight: 'next',
    Home: 'root',
    s: 'solution',
    d: 'deadEnd',
  }
  const HINTS: [string[], string][] = [
    [['↑', '↓'], 'parent / child'],
    [['←', '→'], 'siblings'],
    [['Home'], 'start'],
    [['S'], 'solution'],
    [['D'], 'dead end'],
  ]
  const LEGEND = [
    ['bg-expanded', 'expanded'],
    ['bg-open', 'queued'],
    ['bg-dropped', 'dropped'],
    ['bg-solution', 'solution'],
  ]

  let { params }: { params: URLSearchParams } = $props()

  function onkeydown(event: KeyboardEvent) {
    if (isTyping(event) || !explorer.tree || event.ctrlKey || event.metaKey || event.altKey) return

    const target = KEY_TARGETS[normalizeKey(event)]
    if (!target) return

    event.preventDefault()
    explorer.goTo(target)
  }

  onMount(() => void explorer.init(params))
</script>

<svelte:window {onkeydown} />

<header class="page-header">
  <a class="text-dim no-underline hover:text-fg" href="#/">SokoBot</a>
  <span class="text-dim">/</span>
  <span>Search tree</span>
  <span class="text-dim">/</span>
  <span class="text-dim">{explorer.mapName}</span>
  <span class="flex-1"></span>
  <label class="header-field">
    Thread
    <select class="field" bind:value={explorer.thread}>
      {#each [0, 1, 2, 3, 4] as id (id)}
        <option value={id}>{id}</option>
      {/each}
    </select>
  </label>
  <label class="header-field">
    Max expansions
    <input
      class="field w-20"
      type="number"
      min="0"
      step="1000"
      title="0 = no limit"
      bind:value={explorer.maxExpansions}
    />
  </label>
  <button class="btn btn-primary" disabled={explorer.running} onclick={() => void explorer.run()}>
    Run
  </button>
  <button class="btn btn-outline" disabled={!explorer.running} onclick={() => void explorer.stop()}>
    Stop
  </button>
  <BuildButton onnotice={(next) => (explorer.notice = next)} solver={explorer.solverId || undefined} />
</header>

<div class="flex min-h-8 gap-4 border-b border-line px-4 py-1.5">
  <p data-tone={explorer.notice.tone}>{explorer.notice.text}</p>
  <p class="text-dim">{explorer.summaryText}</p>
</div>

<main
  class={[
    'mx-auto grid w-full max-w-480 items-start gap-6 px-6 pt-4 pb-12',
    'lg:grid-cols-[minmax(0,1fr)_360px]',
  ]}
>
  <div class="flex min-w-0 flex-col gap-4">
    {#if explorer.tree && explorer.summary}
      <KeyMetrics tree={explorer.tree} summary={explorer.summary} />
    {/if}
    <section>
      <div class="mb-2 flex min-h-7 items-center justify-between">
        <h2 class="heading mb-0">The whole tree</h2>
        <p class="flex items-center gap-2 text-xs text-dim">
          {#each LEGEND as [color, label] (label)}
            <span class="flex items-center gap-1"><i class={['size-1.5', color]}></i>{label}</span>
          {/each}
        </p>
      </div>
      <Overview
        tree={explorer.tree}
        selected={explorer.selected}
        empty={explorer.placeholder}
        onpick={(index) => explorer.select(index)}
      />
      <p class="mt-1.5 text-xs text-dim">
        Drag to pan · Scroll to zoom · Click any node to inspect
      </p>
    </section>
    {#if explorer.tree && explorer.summary}
      <Profile tree={explorer.tree} mapName={explorer.mapName} />
      <section>
        <h2 class="heading">Cost breakdown</h2>
        <Stats
          tree={explorer.tree}
          summary={explorer.summary}
          onpick={(index) => explorer.select(index)}
        />
      </section>
    {/if}
  </div>

  {#if explorer.tree && explorer.puzzle}
    <aside class="flex flex-col gap-4">
      <StatePanel
        tree={explorer.tree}
        puzzle={explorer.puzzle}
        selected={explorer.selected}
        rejectedIndex={explorer.rejectedIndex}
        subtreeSizes={explorer.subtreeSizes}
        onselect={(index, rejected) => explorer.select(index, rejected)}
      />
      <p class="flex flex-wrap gap-x-2 text-xs text-dim">
        {#each HINTS as [keys, label] (label)}
          <span
            >{#each keys as key (key)}<kbd>{key}</kbd>{/each}
            {label}</span
          >
        {/each}
      </p>
    </aside>
  {/if}
</main>
