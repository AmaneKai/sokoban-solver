"""Loading, map features, failure classes and run comparison for sokobench CSV files."""

from __future__ import annotations

from pathlib import Path

import pandas as pd

DIRS = ((-1, 0), (1, 0), (0, -1), (0, 1))

# How a thread ended, as written by sokobench in the tN_outcome columns.
OUTCOMES = ("solved", "stopped", "exhausted", "pool_full", "hopeless", "no_memory", "running", "not_run")


# --- runs ------------------------------------------------------------------------------


def load_run(path: str | Path) -> pd.DataFrame:
    """Reads a sokobench CSV. Adds a boolean `solved` column."""
    df = pd.read_csv(path, dtype={"lineup": str, "map": str})
    df["solved"] = df["status"].eq("solved")
    return df


def threads_that_ran(run: pd.DataFrame) -> list[int]:
    """Thread ids whose outcome column is not `not_run` on at least one map."""
    ids = sorted(int(c[1:].split("_")[0]) for c in run.columns if c.endswith("_outcome"))
    return [t for t in ids if (run[f"t{t}_outcome"] != "not_run").any()]


def classify(run: pd.DataFrame) -> pd.Series:
    """Labels each map: solved, out of time, out of memory, or search ran dry.

    - out of memory: every thread that ran filled its state pool (or failed to start)
    - search ran dry: every thread emptied its queue or refused to start, which usually
      means a pruning rule removed all solutions
    - out of time: otherwise (some thread was still searching at the time limit)
    """
    ran = threads_that_ran(run)

    def label(row: pd.Series) -> str:
        if row["solved"]:
            return "solved"
        outs = [row[f"t{t}_outcome"] for t in ran]
        if all(o in ("exhausted", "hopeless") for o in outs):
            return "search ran dry"
        if all(o in ("pool_full", "exhausted", "hopeless", "no_memory") for o in outs):
            return "out of memory"
        return "out of time"

    return run.apply(label, axis=1)


def compare_runs(new: pd.DataFrame, old: pd.DataFrame) -> pd.DataFrame:
    """One row per map present in both runs, with times, winners and `ratio` (new/old)."""
    cols = ["map", "solved", "time_ms", "thread", "nodes"]
    m = new[cols].merge(old[cols], on="map", suffixes=("_new", "_old"))
    m["change"] = "same"
    m.loc[m["solved_new"] & ~m["solved_old"], "change"] = "gained"
    m.loc[~m["solved_new"] & m["solved_old"], "change"] = "lost"
    both = m["solved_new"] & m["solved_old"]
    m["ratio"] = float("nan")
    m.loc[both, "ratio"] = m.loc[both, "time_ms_new"].clip(lower=1) / m.loc[both, "time_ms_old"].clip(lower=1)
    return m


def rate_table(run: pd.DataFrame, column: str, bins: list[float]) -> pd.DataFrame:
    """Maps and solve rate per bin of `column`."""
    groups = pd.cut(run[column], bins=bins)
    return run.groupby(groups, observed=True).agg(maps=("map", "size"), solved=("solved", "mean"))


# --- map features ------------------------------------------------------------------------


def _parse(path: Path):
    rows = path.read_text().rstrip("\n").split("\n")
    width = max(len(r) for r in rows)
    walls, goals, boxes, player = set(), set(), set(), None
    for r, line in enumerate(rows):
        for c, ch in enumerate(line.ljust(width)):
            if ch == "#":
                walls.add((r, c))
            if ch in ".*+":
                goals.add((r, c))
            if ch in "$*":
                boxes.add((r, c))
            if ch in "@+":
                player = (r, c)
    # A wall border around the grid, so floods never leave it.
    for r in range(-1, len(rows) + 1):
        walls.add((r, -1))
        walls.add((r, width))
    for c in range(-1, width + 1):
        walls.add((-1, c))
        walls.add((len(rows), c))
    return walls, goals, boxes, player


def _flood(start, blocked):
    seen, stack = {start}, [start]
    while stack:
        r, c = stack.pop()
        for dr, dc in DIRS:
            n = (r + dr, c + dc)
            if n not in seen and n not in blocked:
                seen.add(n)
                stack.append(n)
    return seen


def _articulation_points(nodes):
    """Cells whose removal disconnects the floor (Tarjan, iterative)."""
    index, low, aps, counter = {}, {}, set(), 0
    for root in nodes:
        if root in index:
            continue
        index[root] = low[root] = counter
        counter += 1
        stack, root_children = [(root, None, iter(DIRS))], 0
        while stack:
            v, parent, it = stack[-1]
            for dr, dc in it:
                w = (v[0] + dr, v[1] + dc)
                if w not in nodes or w == parent:
                    continue
                if w in index:
                    low[v] = min(low[v], index[w])
                else:
                    index[w] = low[w] = counter
                    counter += 1
                    if v == root:
                        root_children += 1
                    stack.append((w, v, iter(DIRS)))
                    break
            else:
                stack.pop()
                if parent is not None:
                    low[parent] = min(low[parent], low[v])
                    if parent != root and low[v] >= index[parent]:
                        aps.add(parent)
        if root_children > 1:
            aps.add(root)
    return aps


def _goal_room_share(floor, walls, goals, boxes, player):
    """Largest share of goals in a region with no boxes and no player, cut off by one cell."""
    best = 0
    for gate in _articulation_points(floor):
        seen = set()
        for dr, dc in DIRS:
            start = (gate[0] + dr, gate[1] + dc)
            if start not in floor or start in seen:
                continue
            region = _flood(start, walls | {gate})
            seen |= region
            if region & boxes or player in region:
                continue
            best = max(best, len(region & goals))
    return best / max(1, len(goals))


def map_features(path: str | Path) -> dict:
    """Static features of one map file."""
    path = Path(path)
    walls, goals, boxes, player = _parse(path)
    floor = _flood(player, walls) if player else set()
    corridor = sum(
        1
        for (r, c) in floor
        if ((r - 1, c) in walls and (r + 1, c) in walls) or ((r, c - 1) in walls and (r, c + 1) in walls)
    )
    return {
        "floor_cells": len(floor),
        "corridor_share": corridor / max(1, len(floor)),
        "box_density": len(boxes) / max(1, len(floor)),
        "boxes_on_goals_at_start": len(boxes & goals),
        "goal_room_share": _goal_room_share(floor, walls, goals, boxes, player) if floor else 0.0,
    }


def add_features(run: pd.DataFrame, maps_dir: str | Path) -> pd.DataFrame:
    """Returns `run` with map features added (maps without a file get NaN)."""
    maps_dir = Path(maps_dir)
    rows = []
    for name in run["map"]:
        path = maps_dir / f"{name}.txt"
        rows.append({"map": name, **map_features(path)} if path.exists() else {"map": name})
    out = run.merge(pd.DataFrame(rows), on="map", how="left")
    out["has_goal_room"] = out["goal_room_share"] >= 0.5
    return out
