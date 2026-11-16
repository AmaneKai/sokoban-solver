import { readCssColor } from '../../canvas/theme.ts'

// svg fills set by Plot don't resolve `var(--x)`, so turn theme tokens into real colors
export const resolveColor = (color: string) =>
  color.startsWith('var(') ? readCssColor(color.slice(4, -1)) : color

export const PLOT_STYLE = {
  background: 'transparent',
  color: 'var(--color-dim)',
  fontSize: '11px',
  overflow: 'visible',
}
