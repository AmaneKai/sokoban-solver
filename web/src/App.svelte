<script lang="ts">
  import Explorer from './pages/Explorer.svelte'
  import Solver from './pages/Solver.svelte'

  let hash = $state(location.hash)
  const [path = '', query = ''] = $derived(hash.slice(1).split('?')) as [string?, string?]
</script>

<svelte:window onhashchange={() => (hash = location.hash)} />

{#if path === '/explore'}
  {#key hash}
    <Explorer params={new URLSearchParams(query)} />
  {/key}
{:else}
  <Solver />
{/if}
