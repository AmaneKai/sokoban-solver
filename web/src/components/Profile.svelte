<script lang="ts">
  import {
    depthProfile,
    depthProfileCsv,
    heuristicHistogram,
    progressCsv,
    progressCurve,
    type SearchTree,
  } from '@sokobot/core'
  import { download } from '../utils.ts'
  import LineChart from './charts/LineChart.svelte'
  import StackedBars from './charts/StackedBars.svelte'

  const MAX_BINS = 40
  const MAX_POINTS = 400
  const FATE_SERIES = [
    { key: 'expanded', label: 'expanded', color: 'var(--color-expanded)' },
    { key: 'open', label: 'queued', color: 'var(--color-open)' },
    { key: 'dropped', label: 'dropped', color: 'var(--color-dropped)' },
    { key: 'solution', label: 'solution', color: 'var(--color-solution)' },
  ]

  // tick labels need to be short: whole ms, one-decimal seconds
  function axisDuration(microseconds: number) {
    if (microseconds < 1_000) return `${Math.round(microseconds)} µs`
    if (microseconds < 1_000_000) return `${Math.round(microseconds / 1_000)} ms`

    return `${+(microseconds / 1_000_000).toFixed(1)} s`
  }

  let { tree, mapName }: { tree: SearchTree; mapName: string } = $props()

  const profile = $derived(depthProfile(tree))
  const histogram = $derived(heuristicHistogram(tree, MAX_BINS))
  const progress = $derived(progressCurve(tree, MAX_POINTS))

  const depthBars = $derived(
    profile.map((row) => ({ label: String(row.depth), values: { ...row } })),
  )
  const heuristicBars = $derived(
    histogram.map((bin) => ({
      label: bin.from === bin.to ? String(bin.from) : `${bin.from}-${bin.to}`,
      values: { ...bin },
    })),
  )
  const branching = $derived(
    profile
      .filter((row) => row.expanded > 0)
      .map((row) => [row.depth, row.averageBranching] as const),
  )
  const bestHeuristic = $derived(progress.map((p) => [p.expansion, p.bestHeuristic] as const))
  const popDepth = $derived(progress.map((p) => [p.expansion, p.depth] as const))
  const cumulativeTime = $derived(
    progress.map((p) => [p.expansion, p.cumulativeMicroseconds] as const),
  )

  const slug = $derived(mapName.replace(/\.txt$/, '') || 'tree')

  function exportDepthCsv() {
    download(`${slug}-depth-profile.csv`, depthProfileCsv(profile), 'text/csv')
  }

  function exportProgressCsv() {
    download(`${slug}-search-progress.csv`, progressCsv(progress), 'text/csv')
  }

  function exportSummaryJson() {
    download(
      `${slug}-summary.json`,
      JSON.stringify(tree.exploration.summary, null, 2),
      'application/json',
    )
  }
</script>

<section>
  <div class="mb-2 flex min-h-7 items-center gap-2">
    <h2 class="heading mb-0">Search profile</h2>
    <span class="flex-1"></span>
    <button class="btn btn-outline h-6 text-xs" onclick={exportDepthCsv}>Depth CSV</button>
    <button class="btn btn-outline h-6 text-xs" onclick={exportProgressCsv}>Progress CSV</button>
    <button class="btn btn-outline h-6 text-xs" onclick={exportSummaryJson}>Summary JSON</button>
  </div>
  <div class="grid gap-x-6 gap-y-5 xl:grid-cols-2">
    <StackedBars
      title="States per depth"
      subtitle="Where the tree is wide, and how those states ended"
      bars={depthBars}
      series={FATE_SERIES}
      xLabel="depth"
      yLabel="states"
    />
    <StackedBars
      title="Heuristic distribution"
      subtitle="Estimated pushes to goal for every generated state"
      bars={heuristicBars}
      series={FATE_SERIES}
      xLabel="heuristic"
      yLabel="states"
    />
    <LineChart
      title="Best heuristic so far"
      subtitle="Search progress: lowest estimate reached after each expansion"
      points={bestHeuristic}
      step
      xLabel="expansion"
      yLabel="best h"
      color="var(--color-expanded)"
    />
    <LineChart
      title="Cumulative solver time"
      subtitle="Steady slope means constant cost per expansion"
      points={cumulativeTime}
      xLabel="expansion"
      yLabel="time"
      formatY={axisDuration}
      color="var(--color-accent)"
    />
    <LineChart
      title="Depth of each expansion"
      subtitle="Shows whether the search dives deep or keeps jumping back to shallow states"
      points={popDepth}
      xLabel="expansion"
      yLabel="depth"
      color="var(--color-dropped)"
    />
    <LineChart
      title="Average branching by depth"
      subtitle="New states generated per expanded state"
      points={branching}
      xLabel="depth"
      yLabel="branching"
      formatY={(value) => value.toFixed(1)}
      color="var(--color-solution)"
    />
  </div>
</section>
