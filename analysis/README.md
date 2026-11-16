# sokostats

Analysis of SokoBot benchmark runs. Standalone: it only reads `sokobench` CSV files and the
map `.txt` files, and never touches the solver.

## Setup

```bash
uv sync                      # text report only
uv sync --extra notebook     # also Jupyter
```

## Produce the data (from the solver repo)

```bash
ls maps | sed 's/\.txt$//' > /tmp/all.txt
SOKO_THREADS=0,1,3,4 solvers/portfolio/bin/sokobench /tmp/all.txt maps 30 analysis/data/new.csv
SOKO_THREADS=0,1,2,3 solvers/portfolio/bin/sokobench /tmp/all.txt maps 30 analysis/data/old.csv
```

Rows are written as each map finishes, so a run can be stopped and the CSV still used.

## Analyse

```bash
uv run crunch data/new.csv --maps ../maps --compare data/old.csv
uv run jupyter lab bench_analysis.ipynb
```

`crunch` prints solve rates, why unsolved maps fail (out of time, out of memory, or a search
that ran dry), solve rate by map feature, thread wins, and the comparison, and writes
`targets.csv` (unsolved and slow maps with their features).

## CSV columns

| Column | Meaning |
|---|---|
| `map`, `status`, `valid` | name; solved / timeout / failed / no_file; 1 if the solution replays correctly |
| `thread`, `moves`, `pushes`, `nodes`, `time_ms` | winning thread and its solution; total expansions; wall time |
| `width`, `height`, `boxes`, `goals`, `floor` | map facts |
| `lineup` | the `SOKO_THREADS` value, or `default` |
| `tN_outcome`, `tN_nodes` | per thread: solved, stopped, exhausted, pool_full, hopeless, no_memory, not_run; its expansions |

Map features added by the analysis: `floor_cells` (reachable floor), `corridor_share`,
`box_density`, `goal_room_share` (largest share of goals behind a single gate cell).
