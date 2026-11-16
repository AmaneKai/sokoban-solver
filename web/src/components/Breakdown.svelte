<script lang="ts">
  import { formatPercent } from '@sokobot/core'

  export interface Slice {
    label: string
    value: number
    text: string
  }

  const MAX_ROWS = 10
  const COLORS = ['#4caf7d', '#ff7369', '#ffd166', '#2383e2', '#b48ead', '#56b6c2']
  const OTHER_COLOR = '#7d8590'

  let {
    title,
    slices,
    format,
  }: { title: string; slices: Slice[]; format?: (value: number) => string } = $props()

  const total = $derived(slices.reduce((sum, slice) => sum + slice.value, 0))
  const shown = $derived.by(() => {
    const top = slices.slice(0, COLORS.length)
    const rest = slices.slice(COLORS.length)
    if (rest.length === 0) return top

    const value = rest.reduce((sum, slice) => sum + slice.value, 0)
    return [...top, { label: `${rest.length} others`, value, text: '' }]
  })

  const rows = $derived.by(() => {
    const top = slices.slice(0, MAX_ROWS)
    const rest = slices.slice(MAX_ROWS)
    if (rest.length === 0) return top

    const value = rest.reduce((sum, slice) => sum + slice.value, 0)
    return [...top, { label: `${rest.length} others`, value, text: format?.(value) ?? '' }]
  })

  const colorOf = (index: number) => COLORS[index] ?? OTHER_COLOR
  const describe = (slice: Slice) =>
    `${slice.label}: ${slice.text || formatPercent(slice.value, total)}`
</script>

<section class="min-w-0">
  <h3 class="mb-1.5 text-[13px] font-medium">{title}</h3>
  {#if slices[0] && total > 0}
    <p class="mb-2 text-dim">
      <b class="text-[22px] font-semibold text-fg">{formatPercent(slices[0].value, total)}</b>
      {slices[0].label}
    </p>
    <div class="mb-2.5 flex h-3.5 gap-0.5">
      {#each shown as slice, index (slice.label)}
        <div
          class="min-w-0.5 rounded-[1px]"
          style:flex={slice.value}
          style:background={colorOf(index)}
          title={describe(slice)}
        ></div>
      {/each}
    </div>
    <table class="w-full text-xs">
      <thead class="text-dim">
        <tr class="h-6 border-b border-line">
          <th class="text-left font-normal">Name</th>
          <th class="pl-4 text-right font-normal">Value</th>
          <th class="w-20 pl-4 text-right font-normal">Share</th>
        </tr>
      </thead>
      <tbody class="[&_td]:py-0.75">
        {#each rows as slice, index (slice.label)}
          <tr class="hover:bg-hover" title={describe(slice)}>
            <td class="flex min-w-0 items-center gap-2"
              ><!-- swatch -->
              <i class="size-2 flex-none rounded-[1px]" style:background={colorOf(index)}></i>
              <span class="truncate" title={slice.label}>{slice.label}</span>
            </td>
            <td class="pl-4 text-right whitespace-nowrap tabular-nums">{slice.text || '-'}</td>
            <td class="pl-4 text-right text-dim tabular-nums"
              >{formatPercent(slice.value, total)}</td
            >
          </tr>
        {/each}
      </tbody>
    </table>
  {/if}
</section>
