# Solver algorithms

This document describes the algorithms of the solver. The solver searches push by push, not move by move. A push moves one box by one cell.

A **state** is one arrangement of boxes plus the normalized player position. The normalized player is the smallest cell index that the player can reach. Two states with the same boxes and the same region are equal.

## Best-first search

The solver keeps open states in a priority queue. An **expansion** pops one state and generates its child states. The solver runs one expansion at a time per thread.

The priority of a child is the sum of five terms. The queue pops the smallest priority first.

| Term | Formula |
| --- | --- |
| Heuristic | `h * h_weight + noise * noise_weight` |
| Switch | `switches * switch_penalty` |
| Depth | `g * (g_weight + g_rate)` |
| Walk | `walk_distance * walk_weight` |
| Tie break | A random value from 0 to 99 |

The solver shifts the sum left by 16 bits. It stores `65535 - g` in the low bits, so a smaller `g` wins a tie.

A **switch** happens when a push moves a different box than the previous push. The switch penalty keeps the search on one box until that box is placed.

The **transposition table** stores every generated state. Each slot packs an 8-bit hash tag and a 24-bit pool index. The solver rejects a child that is already in the table.

## Reverse BFS pull distances

The solver precomputes a push distance for every cell and every goal. It runs one breadth-first search per goal in reverse. The search pulls a box away from the goal.

A box can be pulled from cell `c` to cell `c - d` when two conditions hold. The cell `c - d` is not a wall. The cell `c - 2d` is not a wall, so the player has room to stand.

The result is `goal_dist[cell][goal]`, the minimum number of pushes. The minimum over all goals is `min_dist[cell]`. A cell with no path to any goal is a **dead square**. The solver never pushes a box onto a dead square.

## Matching heuristics

The heuristic estimates the pushes still needed. Boxes already on a goal keep that goal. The remaining boxes are matched to the remaining goals.

### Hungarian matching

The Hungarian algorithm finds the minimum-cost matching between boxes and goals. It runs in $O(n^3)$ time. The cost of a pair is the pull distance.

The algorithm returns the total cost and the matched goal of each box. It returns -1 when a box cannot reach any free goal.

### Greedy matching

Greedy matching handles each box in order. It picks the nearest unused goal. It is faster than the Hungarian algorithm but can fail when two boxes want the same last goal.

Threads with `greedy_exact_fallback` run the Hungarian algorithm after a greedy failure. Only an exact matching may declare a state dead.

### Linear conflict

Two boxes conflict when they share a wall-free line and their goals lie on that line in the opposite order. One box must leave the line. This costs extra pushes.

The solver groups boxes by line and sorts them by position. It finds the longest increasing subsequence of their goal positions. Each box outside that subsequence adds a penalty of 2.

### Incremental update

After one push, the solver reuses the old matching. It assigns the moved box to the nearest free goal. It then tries swapping goals with every other box.

The solver runs the full matching instead in three cases. The moved box lands on a goal. The depth is a multiple of the refresh interval, which is 4 for Hungarian threads and 8 for the others. The heuristic is off.

A state with no valid matching gets the dead value 100,000. The solver drops such a state when dead squares are on.

## Deadlock detection

A deadlock is a state that has no solution. The solver prunes deadlocks early. Each filter on the push path must run in $O(1)$ time or close to it.

### 2x2 blocks

A box deadlocks when it sits in a 2x2 square whose other three cells are walls or boxes. The block is harmless only when every box in it sits on a goal.

### Freeze deadlocks

A box is **frozen** when it cannot move on either axis. A box is blocked on an axis in two cases. A wall touches it on that axis, or both neighbors on that axis are dead squares. A box is also blocked when a neighbor on that axis is a frozen box.

The static part is a precomputed table. The dynamic part is a recursive check. A mark array detects cycles. The solver clears the mark of a box that turns out not to be frozen.

A freeze check on a destination cell that holds no goal prunes the push. For a goal cell, the solver checks the adjacent boxes. With `goal_freeze_check`, it also checks for an empty goal that no box can enter.

### PI-corral pruning

A **corral** is a region of floor that the player cannot reach. Boxes form its barrier. A corral is **PI** (player inaccessible) when the player can open it only by pushing a barrier box inward.

For a PI-corral, only pushes into the corral matter. The solver therefore restricts expansion to those pushes. This removes most of the branching.

When a PI-corral has no valid inward push, the state is a deadlock. A push that lands on a dead square or a frozen box does not count as valid. The solver scans all corrals, not only the first.

Two options change the rules. With `corral_stuck_boxes`, a box that cannot move until the barrier moves does not spoil the PI test. With `corral_optimistic`, the solver skips later pushes from outside the corral. This is faster but can wrongly call a solvable state dead. Only Thread 1 uses it.

### Hall feasibility

The solver checks that every box can still reach a distinct goal. This is a bipartite matching test. It uses Kuhn's augmenting paths on the pull distance graph.

If the test fails, no assignment of boxes to goals exists. The state is dead. The check returns at once when the heuristic already assigned every box.

## Macro moves

### Tunnel macros

A tunnel is a straight, one-cell-wide corridor. A box pushed into it can only slide to the far end. The solver precomputes the end cell and the step count. It then makes the whole slide one push.

### Goal-room packing

A **goal room** is a region with at least two goals. It touches the rest of the board through one gate cell. No box or player may start inside it.

The solver searches for a packing order with a depth-first search. At each step it tries the open goals from the deepest to the nearest. It keeps a goal when a box from the entry can reach it. The player must also be able to return to the gate.

During the search, a box that enters the room jumps to the next goal of the packing order. That push counts as one move. Boxes in the room never move again.

When the room holds every goal and `room_heuristic` is on, the heuristic changes. It sums the push distance of each outside box to the entry. It adds the in-room pushes still to come. No goal matching runs.

### Delivery macros

A **delivery macro** moves one box all the way to the room entry in one step. The solver runs a breadth-first search over the pair (box cell, side the player stands on). The other boxes stay in place.

Two limits bound the cost. `delivery_state_limit` caps the states per attempt. `delivery_candidates` allows only the nearest outside boxes to try.

The solver replans the delivery path when it rebuilds the final move string.

## Stagnation reheaping

Stagnation means the best heuristic has not improved for many pops. The limit is 25,000 pops at first and 4,000 pops during recovery. A reheap changes the priorities to push the search out of a local minimum.

| Mode | Behavior |
| --- | --- |
| `kNone` | No reheap |
| `kFullReheap` | The solver adds a growing weight of `g` to every queued state. New states from the last dive receive the cumulative rate. Older states receive one delta. The first delta is 1,500,000 and later deltas are 150,000. After the heuristic improves, the rate resets |
| `kPocketSink` | The solver pops the best 16,384 states and pushes them back with `g * 1,500,000` added. Deeper states in the queue stay untouched |
| `kBoundedReheap` | The solver recomputes all priorities and rebuilds the heap in $O(n)$. The `g` weight grows by `g_rate_step` up to `max_g_rate`. The switch penalty decays by one quarter toward `switch_penalty_floor`. After the heuristic improves, the weight falls back to one step |

## Thread portfolio

Four thread configurations race in parallel. Each one enables a different mix of the algorithms above. The first solution wins. The file `solvers/portfolio/include/flags.h` holds the exact values.
