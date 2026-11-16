#!/usr/bin/env python3
"""Write a compile_commands.json into each C/C++ solver directory for clangd.

The files hold absolute paths, so they are gitignored; rerun after adding sources.
Flags mirror the Makefiles (Linux branch) minus LTO/-march so clangd stays quiet.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "solvers"

# dir -> (compiler, flags, files that are compiled with -DSOKO_TRACE)
PROJECTS = {
    "classic": ("gcc", ["-Wall", "-Wextra", "-Iinclude", "-pthread"], []),
    "simplified": ("g++", ["-std=c++20", "-Wall", "-Wextra", "-Iinclude", "-pthread"], []),
    "experiments": ("g++", ["-std=c++20", "-Wall", "-Wextra", "-I./include", "-pthread"], ["trace_cli.cpp"]),
    "portfolio": ("g++", ["-std=c++20", "-Wall", "-Wextra", "-I./include", "-pthread"], ["trace_cli.cpp"]),
}

for name, (cc, flags, traced) in PROJECTS.items():
    d = ROOT / name
    srcs = sorted(p for ext in ("c", "cpp") for p in (d / "src").rglob(f"*.{ext}"))
    entries = []
    for src in srcs:
        extra = ["-DSOKO_TRACE"] if src.name in traced else []
        entries.append({
            "directory": str(d),
            "file": str(src),
            "arguments": [cc, *flags, *extra, "-c", str(src)],
        })
    (d / "compile_commands.json").write_text(json.dumps(entries, indent=2) + "\n")
    print(f"{name}: {len(entries)} entries")
