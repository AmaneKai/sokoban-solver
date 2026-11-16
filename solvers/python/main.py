"""Command line front end: a map on stdin, the search time in ms and then the moves on stdout."""
import sys
import time

from solver import SokoBot

GROUND = {"#": "#", ".": ".", "*": ".", "+": "."}
ITEMS = {"$": "$", "*": "$", "@": "@", "+": "@"}


def main():
    rows = sys.stdin.read().splitlines()
    while rows and not rows[-1].strip():
        rows.pop()
    width = max(map(len, rows), default=0)
    if width == 0:
        sys.exit("python: empty map")

    # The ground and the items are separate grids. Short rows are padded with walls.
    rows = [row.ljust(width, "#") for row in rows]
    ground = [[GROUND.get(cell, " ") for cell in row] for row in rows]
    items = [[ITEMS.get(cell, " ") for cell in row] for row in rows]

    start = time.perf_counter()
    moves = SokoBot().solveSokobanPuzzle(width, len(rows), ground, items)
    print(f"{(time.perf_counter() - start) * 1000:.3f}\n{moves}")


main()
