<script lang="ts">
  import * as Plot from '@observablehq/plot'
  import { compact } from './scale.ts'
  import { PLOT_STYLE, resolveColor } from './theme.ts'

  interface Props {
    title: string
    subtitle?: string
    points: readonly (readonly [number, number])[]
    xLabel: string
    yLabel: string
    color?: string
    step?: boolean
    formatY?: (value: number) => string
    formatX?: (value: number) => string
  }

  const HEIGHT = 250

  let {
    title,
    subtitle,
    points,
    xLabel,
    yLabel,
    color = 'var(--color-accent)',
    step = false,
    formatY = compact,
    formatX = compact,
  }: Props = $props()
  let width = $state(0)
  let host = $state<HTMLDivElement>()

  $effect(() => {
    if (!host || width === 0 || points.length < 2) return

    const stroke = resolveColor(color)
    const data = points.map(([x, y]) => ({ x, y }))
    const chart = Plot.plot({
      width,
      height: HEIGHT,
      style: PLOT_STYLE,
      marginLeft: 56,
      x: { label: `${xLabel} →`, tickFormat: formatX, grid: false },
      y: { label: `↑ ${yLabel}`, tickFormat: formatY, grid: true, zero: true },
      marks: [
        Plot.ruleY([0]),
        Plot.line(data, {
          x: 'x',
          y: 'y',
          stroke,
          strokeWidth: 1.75,
          curve: step ? 'step-after' : 'linear',
        }),
        Plot.dot(data, Plot.pointerX({ x: 'x', y: 'y', fill: stroke, r: 3.5 })),
        Plot.tip(
          data,
          Plot.pointerX({
            x: 'x',
            y: 'y',
            format: {
              x: (d: number) => `${xLabel} ${formatX(d)}`,
              y: (d: number) => `${yLabel} ${formatY(d)}`,
            },
          }),
        ),
      ],
    })

    host.replaceChildren(chart)
    return () => chart.remove()
  })
</script>

<section class="chart min-w-0">
  <h3 class="text-[13px] font-medium">{title}</h3>
  {#if subtitle}<p class="mb-1 text-xs text-dim">{subtitle}</p>{/if}
  {#if points.length > 1}
    <div bind:this={host} bind:clientWidth={width} class="mt-1" style:height="{HEIGHT}px"></div>
  {:else}
    <p class="py-8 text-center text-xs text-dim">Not enough data to plot.</p>
  {/if}
</section>
