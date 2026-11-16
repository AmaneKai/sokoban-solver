# Architecture

SokoBot has four systems that connect through narrow interfaces. This document describes each system and the boundary between them.

## Overview

A **solver** is a program that reads a map on stdin and prints the search time in milliseconds and then a move string. Each solver lives in its own folder in `solvers/`. The main one, `portfolio`, is a C++20 search engine. A second build of it, `sokotrace`, also records a search tree.

The **core system** is the shared TypeScript package `@sokobot/core`. It owns all domain logic, the state controllers, and the engine services. It imports no DOM, `window`, Canvas, or Svelte API.

The **web UI** is a Svelte 5 browser interface with a Hono HTTP server. Both the web UI and the CLI scripts call the core system and nothing else.

```
  web UI (Svelte 5)                                     scripts (CLI)
        |                                                    |
  HttpEngineService                                    core exporters
        |                                                    |
   Hono server ----------------> LocalEngineService <--------+
                                       |
                                  solver binaries
                         sokosolve  sokotrace  sokobench
```

## Solver modules

The directory `solvers/portfolio/include/soko/` holds one header per responsibility. The directory `solvers/portfolio/src/` holds the matching source files. All code lives in the namespace `soko`.

| Module | Responsibility |
| --- | --- |
| `types.h` | Fixed-width integers, constants, the `SokoState` layout, push bit flags, and aligned allocators |
| `board.h`, `board.cpp` | The `Board` struct. It holds the static topology, Zobrist tables, pull distances, line segments, and tunnel macros |
| `map_loader.h` | One parser for map text and one helper that builds the `char**` row pointers |
| `state_pool.h` | `StatePool` allocates `SokoState` records. `TranspositionTable` detects repeated states with 32-bit packed hashes |
| `priority_queue.h` | `PriorityQueue` is the 4-ary min-heap. It also holds the reheaping routines |
| `reachability.h`, `reachability.cpp` | `Reachability` holds the box layout. It runs the player flood fill, the shortest walk path, and the cut-vertex test |
| `heuristic.h`, `heuristic.cpp` | `HeuristicEngine` runs Hungarian matching, greedy matching, linear conflict, and incremental updates |
| `deadlock.h`, `deadlock.cpp` | `DeadlockDetector` runs the freeze tables, 2x2 checks, PI-corral search, and Hall feasibility |
| `goal_room.h`, `goal_room.cpp` | `GoalRoom` detects rooms, plans the packing order, and runs the delivery macro search |
| `solver.h`, `solver.cpp` | `SearchWorker` coordinates the modules. The file also holds the C ABI function `SolveSokoban` |

The public header `solvers/portfolio/include/sokoban.h` declares `SolveSokoban`, the global counters, and the trace event types. The header `solvers/portfolio/include/flags.h` declares the four thread configurations.

### Rules for the hot loop

The expansion loop runs millions of times per second. Four rules protect its speed:

1. The loop uses no virtual functions.
2. The loop uses no `std::function`.
3. The loop makes no heap allocation.
4. Small helpers are `inline` and `noexcept` in headers, so `-O3 -flto` produces tight code.

The struct `SokoState` has a 24-byte header followed by a flexible array. A `static_assert` enforces the size.

### Threads and the C ABI

`SolveSokoban` starts one `SearchWorker` per selected thread configuration. The first worker to find a solution sets `g_solution_found`. The other workers see the flag and stop.

Two build flavors exist. The default build produces `sokosolve` and `sokobench`. The build with `-DSOKO_TRACE` produces `sokotrace`. The trace build emits an event for each pop, child, and prune.

## Core system

The package `@sokobot/core` lives in `core/`. Its entry point is `core/src/index.ts`. The file `core/src/index.browser.ts` is the browser-safe entry. It leaves out the modules that need Node or Bun.

| Path | Content |
| --- | --- |
| `src/domain/contract.ts` | Status and summary types: `SolveStatus`, `ExploreStatus`, `ExploreSummary`, `JobState` |
| `src/domain/puzzle.ts` | Map parsing, player movement, box pushing, and undo history |
| `src/domain/exploration.ts` | Binary trace decoding and the prune reason constants |
| `src/domain/search-tree.ts` | Tree construction, heavy-path lane layout, subtree sizes, and node navigation |
| `src/domain/tree-inspect.ts` | Node details, box switch statistics, rejected pushes, and board reconstruction |
| `src/domain/tree-profile.ts` | Depth profile, heuristic histogram, progress curve, and CSV exporters |
| `src/client/` | The `EngineService` interface and its two implementations |
| `src/controllers/` | `SolverController` and `ExplorerController` |

### The EngineService protocol

`EngineService` is the only way a UI reaches the solver. It has ten methods.

| Method | Action |
| --- | --- |
| `listMaps` | Returns the map names and whether the default solver is built |
| `listSolvers` | Returns every solver with its name, build state, and whether it has an explorer and a thread picker |
| `loadMap` | Returns the text of one map |
| `build` | Compiles one solver. With no id it builds the default one |
| `startSolve` | Starts a solve job with the chosen solver. It returns an error message or `null` |
| `solveStatus` | Returns the state of the solve job |
| `cancelSolve` | Kills the solve job |
| `startExplore` | Starts a trace job. It returns an error message when the solver has no tracer, or `null` |
| `exploreStatus` | Returns the state of the trace job |
| `stopExplore` | Asks the trace job to stop and keep its tree |
| `fetchExploration` | Returns the decoded search tree of the last trace job |

Two classes implement the interface:

- `LocalEngineService` runs the solver binaries as child processes. It allows one job at a time.
- `HttpEngineService` calls the HTTP API with `fetch`. The web UI uses it in the browser.

### Solvers

`LocalEngineService` scans `solvers/` on every call. A folder with a `Makefile` is a solver, so a new folder appears without a restart. An optional `solver.json` sets the name, the binary names, the make targets, extra arguments and environment variables, the output format, and the systems the solver builds on. The README lists every key.

A solver can run Solve with only a `sokosolve` binary. It can open the Explorer only when `solver.json` names a `trace` binary. `startExplore` rejects any other solver, and the web UI greys out Explore for it. The default solver is `portfolio`.

### Controllers

`SolverController` and `ExplorerController` hold the state of each view. They talk only to `EngineService`. Each controller replaces its state object after every change and then notifies its listeners. A UI subscribes to the controller and redraws.

## Web UI

The directory `web/` holds a Svelte 5 interface and a Hono server.

The file `web/server/api.ts` maps each HTTP route to one `LocalEngineService` method. It holds no job logic.

The files `web/src/stores/solver.svelte.ts` and `web/src/stores/explorer.svelte.ts` wrap the controllers. Each wrapper copies the controller state into a Svelte rune. The Svelte components read the wrapper and call its methods.

## Terminal summary

The module `scripts/summary.ts` renders the static summary that `bun run solve <map> --summary` prints. It uses plain ANSI text and has no UI dependency.
