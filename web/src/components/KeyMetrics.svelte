<script lang="ts">
  import { keyMetrics, type ExploreSummary, type SearchTree } from '@sokobot/core'

  let { tree, summary }: { tree: SearchTree; summary: ExploreSummary } = $props()

  const tiles = $derived(keyMetrics(tree, summary))
</script>

<section aria-label="Key metrics">
  <h2 class="heading">Run summary</h2>
  <div class="grid grid-cols-[repeat(auto-fill,minmax(10.5rem,1fr))] gap-2">
    {#each tiles as tile (tile.label)}
      <div class="box px-3 py-2.5">
        <p class="text-[11px] tracking-wide text-dim uppercase">{tile.label}</p>
        <b class="block text-xl font-semibold text-fg tabular-nums">{tile.value}</b>
        <p class="text-[11px] text-dim">{tile.hint}</p>
      </div>
    {/each}
  </div>
</section>
