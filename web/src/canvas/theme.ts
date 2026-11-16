const cssColors = new Map<string, string>()

export function readCssColor(name: string): string {
  let color = cssColors.get(name)
  if (color === undefined) {
    color = getComputedStyle(document.documentElement).getPropertyValue(name).trim()
    cssColors.set(name, color)
  }

  return color
}

export const parseHexColor = (hex: string): readonly number[] => [
  parseInt(hex.slice(1, 3), 16),
  parseInt(hex.slice(3, 5), 16),
  parseInt(hex.slice(5, 7), 16),
]

export const packColor = (red: number, green: number, blue: number) =>
  0xff000000 | (blue << 16) | (green << 8) | red
