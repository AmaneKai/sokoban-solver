<script lang="ts">
  import * as Plot from '@observablehq/plot'
  import { compact } from './scale.ts'
  import { PLOT_STYLE, resolveColor } from './theme.ts'

  export interface Series {
    key: string
    label: string
    color: string
  }

  export interface Bar {
    label: string
    values: Record<string, number>
  }

  interface Props {
    title: string
    subtitle?: string
    bars: Bar[]
    series: Series[]
    xLabel: string
    yLabel: string
    canLog?: boolean
  }

  const HEIGHT = 250
  const MAX_X_TICKS = 10

  let { title, subtitle, bars, series, xLabel, yLabel, canLog = true }: Props = $props()
  let isLog = $state(false)
  let width = $state(0)
  let host: HTMLDivElement

  $effect(() => {
    if (width === 0) return

    const rows = bars.flatMap((bar) =>
      series.map((s) => ({ x: bar.label, series: s.label, value: bar.values[s.key] ?? 0 })),
    )
    const step = Math.max(1, Math.ceil(bars.length / MAX_X_TICKS))
    const chart = Plot.plot({
      width,
      height: HEIGHT,
      style: PLOT_STYLE,
      marginLeft: 48,
      x: {
        label: xLabel,
        domain: bars.map((bar) => bar.label),
        ticks: bars.filter((_, index) => index % step === 0).map((bar) => bar.label),
      },
      y: {
        label: `↑ ${yLabel}${isLog ? ' (log)' : ''}`,
        type: isLog ? 'symlog' : 'linear',
        grid: true,
        tickFormat: compact,
      },
      color: {
        domain: series.map((s) => s.label),
        range: series.map((s) => resolveColor(s.color)),
      },
      marks: [
        Plot.barY(rows, {
          x: 'x',
          y: 'value',
          fill: 'series',
          order: series.map((s) => s.label),
          insetLeft: 0.5,
          insetRight: 0.5,
          tip: { format: { x: (d: string) => `${xLabel} ${d}`, fill: true } },
        }),
        Plot.ruleY([0]),
      ],
    })

    host.replaceChildren(chart)
    return () => chart.remove()
  })
</script>

<section class="chart min-w-0">
  <div class="mb-1 flex items-start gap-2">
    <div>
      <h3 class="text-[13px] font-medium">{title}</h3>
      {#if subtitle}<p class="text-xs text-dim">{subtitle}</p>{/if}
    </div>
    {#if canLog}
      <label class="ml-auto flex items-center gap-1 text-xs whitespace-nowrap text-dim">
        <input type="checkbox" bind:checked={isLog} /> log scale
      </label>
    {/if}
  </div>
  <ul class="mb-1 flex flex-wrap gap-x-3 text-xs text-dim">
    {#each series as s (s.key)}
      <li class="flex items-center gap-1">
        <i class="size-2" style:background={s.color}></i>{s.label}
      </li>
    {/each}
  </ul>
  <div bind:this={host} bind:clientWidth={width} style:height="{HEIGHT}px"></div>
</section>
