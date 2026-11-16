"""`crunch`: a text report for one sokobench run, optionally compared with another.

    uv run crunch data/new.csv --maps ../maps --compare data/old.csv
"""

from __future__ import annotations

import argparse
from pathlib import Path

import pandas as pd

from sokostats.core import add_features, classify, compare_runs, load_run, rate_table, threads_that_ran


def _heading(text: str) -> None:
    print(f"\n== {text} " + "=" * max(0, 70 - len(text)))


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(prog="crunch", description=__doc__.split("\n\n")[0])
    parser.add_argument("run", type=Path, help="sokobench CSV to analyse")
    parser.add_argument("--maps", type=Path, default=Path("maps"), help="folder with <map>.txt files")
    parser.add_argument("--compare", type=Path, help="second CSV (for example the old line-up)")
    parser.add_argument("--slow-ms", type=float, default=1000, help="solves slower than this are targets")
    parser.add_argument("--targets", type=Path, default=Path("targets.csv"), help="where to write targets")
    args = parser.parse_args(argv)
    pd.set_option("display.width", 160, "display.max_columns", 30)

    run = load_run(args.run)
    has_maps = args.maps.is_dir()
    if has_maps:
        run = add_features(run, args.maps)
    run["result"] = classify(run)
    ran = threads_that_ran(run)

    _heading("overview")
    print(f"maps {len(run)}   line-up {run['lineup'].iloc[0]!r}   threads that ran {ran}")
    print(f"solved {run['solved'].sum()} ({run['solved'].mean():.1%})")
    invalid = run[run["solved"] & (run["valid"] != 1)]
    print(f"invalid solutions {len(invalid)}" + (f": {', '.join(invalid['map'][:10])}" if len(invalid) else ""))
    print(run["result"].value_counts().to_string())

    unsolved = run[~run["solved"]]
    if len(unsolved):
        _heading("how each thread ended on unsolved maps")
        table = pd.DataFrame({f"T{t}": unsolved[f"t{t}_outcome"].value_counts() for t in ran}).fillna(0).astype(int)
        print(table.to_string())

    if has_maps:
        _heading("solve rate by feature")
        for column, bins in [
            ("boxes", [0, 4, 6, 8, 10, 14, 20, 40]),
            ("floor_cells", [0, 50, 100, 150, 200, 300, 2000]),
            ("corridor_share", [0, 0.1, 0.2, 0.3, 0.4, 0.6, 1]),
        ]:
            table = rate_table(run, column, bins)
            table["solved"] = (table["solved"] * 100).round(1)
            print(f"\n{column}\n{table.to_string()}")
        print("\ngoal room (>= half the goals behind one gate)")
        print(run.groupby("has_goal_room")["solved"].agg(maps="size", solved="mean").round(3).to_string())
        if len(unsolved):
            print("\nfailure class by box count")
            groups = pd.cut(unsolved["boxes"], [0, 6, 10, 14, 20, 40])
            print(unsolved.groupby(groups, observed=True)["result"].value_counts().unstack(fill_value=0).to_string())

    solved = run[run["solved"]]
    _heading("speed and winners")
    print(f"median solve {solved['time_ms'].median():.0f} ms; slower than {args.slow_ms:.0f} ms: "
          f"{(solved['time_ms'] > args.slow_ms).sum()}")
    wins = solved["thread"].value_counts().sort_index()
    slow = solved[solved["time_ms"] > args.slow_ms / 10]["thread"].value_counts().sort_index()
    print(pd.DataFrame({"wins": wins, f"wins > {args.slow_ms / 10:.0f} ms": slow}).fillna(0).astype(int)
          .rename(index=lambda t: f"T{t}").to_string())

    if args.compare:
        other = load_run(args.compare)
        m = compare_runs(run, other)
        _heading(f"compared with {args.compare.name} (line-up {other['lineup'].iloc[0]!r})")
        both = m.dropna(subset=["ratio"])
        print(f"solved: this {m['solved_new'].sum()}, other {m['solved_old'].sum()}   "
              f"gained {(m['change'] == 'gained').sum()}, lost {(m['change'] == 'lost').sum()}")
        print(f"both solve {len(both)}: median time ratio {both['ratio'].median():.2f}, "
              f"total {both['time_ms_new'].sum() / 1e3:.1f} s vs {both['time_ms_old'].sum() / 1e3:.1f} s")
        print(f">2x slower {(both['ratio'] > 2).sum()}, >2x faster {(both['ratio'] < 0.5).sum()}")
        for label in ("lost", "gained"):
            rows = m[m["change"] == label]
            if len(rows):
                print(f"\n{label}: {', '.join(rows['map'][:30])}")
        big = both.assign(saved_ms=both["time_ms_old"] - both["time_ms_new"]).sort_values("saved_ms")
        print("\nbiggest slowdowns\n" + big.head(8)[["map", "time_ms_old", "time_ms_new", "thread_old", "thread_new"]].to_string(index=False))
        print("\nbiggest speed-ups\n" + big.tail(8)[["map", "time_ms_old", "time_ms_new", "thread_old", "thread_new"]].to_string(index=False))

    targets = run[~run["solved"] | (run["time_ms"] > args.slow_ms)]
    cols = ["map", "result", "time_ms", "boxes"] + (["floor_cells", "corridor_share", "goal_room_share"] if has_maps else []) \
        + [f"t{t}_outcome" for t in ran]
    targets.sort_values(["solved", "time_ms"], ascending=[True, False])[cols].to_csv(args.targets, index=False)
    _heading("targets")
    print(f"{len(targets)} unsolved or slow maps written to {args.targets}")


if __name__ == "__main__":
    main()
