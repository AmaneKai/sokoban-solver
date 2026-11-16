<script lang="ts">
  import {
    formatCount,
    formatDuration,
    computeTreeStats,
    PHASE_LABELS,
    PRUNE_LABELS,
    slowestSteps,
    stateDetails,
    type ExploreSummary,
    type PruneReason,
    type SearchTree,
  } from '@sokobot/core'
  import Breakdown, { type Slice } from './Breakdown.svelte'

  const PRUNING_RULES: PruneReason[] = [
    'outsideCorral',
    'deadSquare',
    'twoByTwo',
    'freeze',
    'deadHeuristic',
    'duplicate',
  ]
  const SLOWEST_STEP_COUNT = 8

  let {
    tree,
    summary,
    onpick,
  }: { tree: SearchTree; summary: ExploreSummary; onpick: (index: number) => void } = $props()

  const bySize = (a: Slice, b: Slice) => b.value - a.value
  const stats = $derived(computeTreeStats(tree))

  const pushSlices = $derived(
    [
      { label: 'Became state', value: stats.generatedCount },
      ...PRUNING_RULES.map((reason) => ({
        label: PRUNE_LABELS[reason],
        value: stats.rejectedByReason.get(reason) ?? 0,
      })).filter((slice) => slice.value > 0),
    ]
      .map((slice) => ({ ...slice, text: formatCount(slice.value) }))
      .sort(bySize),
  )

  const timeSlices = $derived(
    summary.phaseNanoseconds
      .map((nanoseconds, index) => ({
        label: PHASE_LABELS[index] ?? `Phase ${index}`,
        value: nanoseconds,
        text: formatDuration(nanoseconds),
      }))
      .filter((slice) => slice.value > 0)
      .sort(bySize),
  )

  const slowest = $derived(
    slowestSteps(tree, SLOWEST_STEP_COUNT).map((index) => {
      const details = stateDetails(tree, index)
      return {
        index,
        microseconds: details.stepMicroseconds,
        depth: details.depth,
        heuristic: details.heuristic,
      }
    }),
  )
  const slowestTime = $derived(slowest[0]?.microseconds ?? 0)
</script>

<div class="grid gap-x-8 gap-y-6 xl:grid-cols-2">
  <Breakdown title="Where the pushes went" slices={pushSlices} format={formatCount} />
  <Breakdown title="Where the time went" slices={timeSlices} format={formatDuration} />
  <section class="xl:col-span-2">
    <h3 class="mb-1.5 text-[13px] font-medium">Slowest steps</h3>
    <table class="w-full text-xs">
      <thead class="text-dim">
        <tr class="h-6 border-b border-line">
          <th class="text-left font-normal">State</th>
          <th class="pl-6 text-right font-normal">Depth</th>
          <th class="pl-6 text-right font-normal">h</th>
          <th class="pl-6 text-right font-normal whitespace-nowrap">Time</th>
          <th class="w-1/2"></th>
        </tr>
      </thead>
      <tbody>
        {#each slowest as { index, microseconds, depth, heuristic } (index)}
          <tr class="row-button h-6" onclick={() => onpick(index)}>
            <td class="px-1">#{formatCount(index)}</td>
            <td class="pl-6 text-right tabular-nums">{depth}</td>
            <td class="pl-6 text-right tabular-nums">{heuristic}</td>
            <td class="pl-6 text-right whitespace-nowrap tabular-nums">
              {formatDuration(microseconds * 1_000)}
            </td>
            <td class="pl-4">
              <i
                class="block h-1.5 rounded-[1px] bg-accent"
                style:width="{slowestTime > 0 ? (microseconds / slowestTime) * 100 : 0}%"
              ></i>
            </td>
          </tr>
        {/each}
      </tbody>
    </table>
  </section>
</div>
