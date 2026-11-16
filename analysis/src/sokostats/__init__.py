"""Analysis of SokoBot benchmark runs.

Load a sokobench CSV with `load_run`, add map features with `add_features`, classify
failures with `classify`, and compare two runs with `compare_runs`.
"""

from sokostats.core import (
    add_features,
    classify,
    compare_runs,
    load_run,
    map_features,
    rate_table,
    threads_that_ran,
)

__all__ = [
    "add_features",
    "classify",
    "compare_runs",
    "load_run",
    "map_features",
    "rate_table",
    "threads_that_ran",
]
