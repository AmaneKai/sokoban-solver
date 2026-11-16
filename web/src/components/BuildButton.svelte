<script lang="ts">
  import type { Notice } from '@sokobot/core'
  import { engine } from '../stores/solver.svelte.ts'

  let { onnotice, solver }: { onnotice: (notice: Notice) => void; solver?: string } = $props()
  let building = $state(false)

  async function build() {
    building = true
    onnotice({ text: 'Compiling solver…', tone: '' })
    const result = await engine.build(solver)
    building = false
    onnotice(
      result.ok
        ? { text: 'Solver built.', tone: 'ok' }
        : { text: `Build failed: ${result.error}`, tone: 'bad' },
    )
  }
</script>

<button class="btn" title="Recompile selected solver" disabled={building} onclick={build}>Build</button>
