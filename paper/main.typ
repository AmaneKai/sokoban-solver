// SokoBot project report. Compile with:  typst compile main.typ
// Figures are regenerated from data/ with:  python3 make_figures.py

#set document(
  title: "Switch Penalties, Goal Rooms and Backward Search: An Empirical Study of a Portfolio Sokoban Solver",
)
#set page(paper: "a4", margin: (x: 2.2cm, y: 2.4cm), numbering: "1")
#set text(font: ("Libertinus Serif", "New Computer Modern"), size: 10pt, lang: "en")
#set par(justify: true, leading: 0.62em, spacing: 1.1em)
#set heading(numbering: "1.1")
#show heading.where(level: 1): it => block(above: 1.6em, below: 0.9em, text(size: 12pt, it))
#show heading.where(level: 2): it => block(above: 1.3em, below: 0.7em, text(size: 10.5pt, it))
#set math.equation(numbering: "(1)")
#show figure.caption: set text(size: 8.5pt)
#show figure: set block(breakable: false)
#set table(stroke: none, inset: (x: 5pt, y: 3.2pt))
#show table: set text(size: 8.5pt)
#show raw: set text(font: "DejaVu Sans Mono", size: 8pt)

// ---------- theorem-like environments (one shared counter) ----------
#let thm-counter = counter("statement")
#let statement(kind) = (name: none, body) => block(width: 100%, above: 1.0em, below: 1.0em)[
  #thm-counter.step()
  *#kind #context thm-counter.display().*
  #if name != none [_(#name)._]
  #body
]
#let definition = statement("Definition")
#let proposition = statement("Proposition")
#let lemma = statement("Lemma")
#let remark = statement("Remark")
#let observation = statement("Observation")
#let proof(body) = block(width: 100%, above: 0.6em, below: 1.0em)[
  _Proof._ #body #h(1fr) $square$
]

// ---------- board drawing ----------
#let c-wall = rgb("#4a4945")
#let c-box = rgb("#eda100")
#let c-home = rgb("#1baf7a")
#let c-goal = rgb("#2a78d6")
#let c-room = rgb("#cde2fb")
#let c-corral = rgb("#f6d4c4")
#let c-hot = rgb("#eb6834")

#let board(rows, room: (), corral: (), hot: (), gate: none, size: 8.5pt) = {
  let w = calc.max(..rows.map(r => r.len()))
  let cells = ()
  for (r, line) in rows.enumerate() {
    for c in range(w) {
      let ch = if c < line.len() { line.at(c) } else { " " }
      let rc = (r, c)
      let bg = if ch == "#" { c-wall }
        else if room.contains(rc) { c-room }
        else if corral.contains(rc) { c-corral }
        else { white }
      let inner = if ch == "$" or ch == "*" {
        let stroke = if hot.contains(rc) { 1.4pt + c-hot } else { 0.5pt + rgb("#5f5e58") }
        rect(width: 78%, height: 78%, radius: 1.2pt,
             fill: if ch == "*" { c-home } else { c-box }, stroke: stroke)
      } else if ch == "." {
        circle(radius: size * 0.17, fill: c-goal)
      } else if ch == "@" {
        circle(radius: size * 0.3, fill: rgb("#1f1f1e"))
      } else if gate == rc {
        text(size: size * 0.75, fill: rgb("#1f1f1e"))[*G*]
      } else if hot.contains(rc) {
        text(size: size * 0.8, fill: c-hot)[*×*]
      } else { none }
      let first-wall = line.position("#")
      let outside = first-wall == none or c < first-wall or c >= line.len()
      let edge = if ch == "#" or (outside and bg == white) { none } else { 0.2pt + rgb("#d8d7d1") }
      cells.push(box(width: size, height: size, fill: bg, stroke: edge,
                     align(center + horizon, inner)))
    }
  }
  grid(columns: (size,) * w, rows: size, ..cells)
}

#let map-1-11 = (
  "          ####",
  "     #### #  #",
  "   ### @###$ #",
  "  ##      $  #",
  " ##  $ $$## ##",
  " #  #$##     #",
  " # # $ $$ # ###",
  " #   $ #  # $ #####",
  "####    #  $$ #   #",
  "#### ## $         #",
  "#.    ###  ########",
  "#.. ..# ####",
  "#...#.#",
  "#.....#",
  "#######",
)
#let room-1-11 = (
  (10, 1), (10, 2), (10, 3), (10, 4), (10, 5),
  (11, 1), (11, 2), (11, 3), (11, 4), (11, 5),
  (12, 1), (12, 2), (12, 3), (12, 5),
  (13, 1), (13, 2), (13, 3), (13, 4), (13, 5),
)
#let map-c10 = (
  "             #####",
  "##############   #",
  "#  $ $ $ $ $ $ # #",
  "#  # # # # # # # #",
  "#  #             #",
  "#  # ###### # # ##",
  "#@ . . . . . .   #",
  "##########       #",
  "         ######### ",
)
#let trap-1-11 = (
  "          ####",
  "     #### #  #",
  "   ###  ###  #",
  "  ##     $ $ #",
  " ##      ##$##",
  " #  # ##     #",
  " # #      # ###",
  " #     #  #   #####",
  "####    #     #   #",
  "#### ##           #",
  "#*    ###  ########",
  "#*. ..# ####",
  "#***#*#",
  "#*****#",
  "#######",
)

// =====================================================================================
// Title block
// =====================================================================================
#align(center)[
  #text(size: 16pt, weight: "bold")[Switch Penalties, Goal Rooms and Backward Search:\ An Empirical Study of a Portfolio Sokoban Solver]
  #v(0.8em)
  #text(size: 9pt)[Personal project report, revised 14 November 2026]
]

#v(0.6em)
#block(inset: (x: 1.2cm))[
  #set text(size: 9pt)
  *Abstract.* SokoBot is a C++20 Sokoban solver that races several greedy best-first
  threads, each with its own priority function and pruning rules. Its central design element is
  a _box-switch penalty_: a weighted count of how often a path changes which box it pushes,
  added to every state's priority. Unlike the inertia tie-breaking used in earlier optimal
  solvers, the penalty is cumulative and global, and it trades directly against heuristic
  progress. A local model of that trade predicts an optimal exchange rate
  $2 <= w_sigma slash w_h < 3$, and a five-seed sweep confirms the lower edge but not the
  upper one. One failure is diagnosed in depth: on level 11 of _Original & Extra_ the
  strongest thread fills a $10^7$-state pool because its stagnation recovery drives the
  depth weight to $lambda approx 115$, turning best-first search into a breadth-first
  sweep. Bounded recovery, a single-entrance goal-room decomposition, a delivery macro and
  refined PI-corral rules take that level from unsolved to $1.25 dot 10^5$ expansions. A
  backward thread that pulls boxes from the solved position solves the corridor level C-10
  in 7 813 expansions, against $8.9 dot 10^5$ for the best forward thread. The switch
  penalty turns out to be direction-dependent: it focuses forward search but breaks backward
  search, where level 1-13 is solved in $7.2 dot 10^5$ expansions without it and exhausts
  $10^7$ states with it. On the full
  2 746-level benchmark with a 10 s limit, the redesigned portfolio raises coverage from
  90.7% [89.6, 91.7] to 95.6% [94.8, 96.3] (exact McNemar $p = 7.7 dot 10^(-32)$; 145 levels
  gained, 10 lost), a gain that comes from replacing one forward thread with the backward
  thread. On the 2 481 levels both portfolios solve, it is 1.31× faster (geometric
  mean, 95% CI 1.26–1.37) and cuts total solve time from 423 s to 181 s. Unsolved levels are
  almost all time-outs, but threads ran out of states on up to 114 levels that another
  configuration solves, which measures the cost of the solver's incomplete pruning. From these
  findings follows, as a proposal, a single-threaded bidirectional algorithm that
  penalises switches forward only. Negative results and limitations are reported in full.
]

// =====================================================================================
= Introduction
// =====================================================================================

Sokoban is PSPACE-complete @culberson1999 and NP-hard even in restricted forms
@dor1999, yet small hand-designed levels remain a standard testbed for domain-enhanced
single-agent search @junghanns2001 @shoham2020. Practical solvers combine a search
skeleton with a large set of domain rules: deadlock detection, macro moves, lower
bounds from bipartite matching, and problem decompositions @junghanns1999 @botea2002.

This document records what was built, measured and learned in the development of one such
solver, SokoBot, a personal project that grew out of a university course assignment
(@sec:history). It is a
research log organised as a report, not a formal thesis: it favours stating exactly what
was done and what was measured over academic convention. It
races greedy best-first threads that share a board model but differ in priority function
and pruning. Its most distinctive element is the _box-switch penalty_: every state carries
the number $sigma$ of times its path switched from pushing one box to pushing another, and
$sigma$ enters the priority with weight $w_sigma$. The penalty encodes a human habit:
finish moving one box before starting on the next. Related ideas exist. Rolling Stone
orders moves to prefer the last-pushed box @junghanns2001, and optimal solvers use
_inertia_ to break ties between states of equal $f$-value in favour of continuing with the
same box @zubaran2011 @pereira2016. Those are local tie-breakers inside an admissible
search. The switch penalty is a cumulative cost on the whole path that competes directly
with heuristic progress in a satisficing search, so its weight is a real design parameter
with its own failure modes (@sec:exchange).

After the formal setting (@sec:formal) and an account of how the solver reached its
baseline (@sec:history), the report has two parts. The first is a case study of one failure: level 11 of the
_Original & Extra_ collection (19 × 15, fourteen boxes, a fourteen-goal room in the lower
left; @fig:board), where the solver's best thread runs out of memory. Rather than tuning
blindly, the search was instrumented, the failure diagnosed mathematically, and changes
derived from the diagnosis. The second part evaluates the resulting portfolio, now with a
backward-search thread, on the full 2 746-level benchmark with paired statistical tests.
What the report covers:

+ *The switch penalty and its exchange rate* (@sec:switch, @sec:exchange): a definition
  that separates it from inertia tie-breaking, a sibling-order model predicting an optimal
  band for $w_sigma slash w_h$, and a seeded sweep that confirms the lower edge and refutes
  the upper one.
+ *A failure model* (@sec:failure) linking the observed telemetry (per-expansion cost
  growing from 3.7 µs to 13.7 µs, 58.5% of time in queue maintenance, 85% of states at
  depth $<= 60$) to the stagnation-recovery schedule, including a closed-form estimate of
  the effective depth weight.
+ *Four search changes* derived from that model: bounded recovery (@sec:recovery), a
  goal-room decomposition (@sec:room), a delivery macro (@sec:delivery) and corral-deadlock
  refinements (@sec:corral), each with a soundness argument or a measured soundness
  failure.
+ *A backward-search thread* (@sec:backward) that pulls boxes from the solved position
  towards the start, solving goal-ordering traps that defeat every forward thread.
+ *Direction dependence of the switch penalty* (@sec:direction): the penalty that focuses
  forward search prevents backward search from solving levels it otherwise solves, with a
  mechanism and telemetry.
+ *A portfolio redesign and full evaluation* (@sec:portfolio, @sec:bench): a
  marginal-contribution measure for choosing threads, and a paired comparison on 2 746
  levels with Wilson intervals, an exact McNemar test, a Wilcoxon signed-rank test and a
  failure taxonomy, followed by telemetry of the remaining failures (@sec:residual).
+ *A proposed single-threaded algorithm* (@sec:proposal) that combines the findings:
  forward and backward lanes taking turns, a switch penalty in the forward lane only, a
  meeting table and a retry for incomplete pruning.

Several of the arguments were also machine-checked in Lean 4 (Appendix B). A
machine-checked proof certifies a model of the solver, not the solver itself, so every such
claim that predicts behaviour is paired with an experiment, and two of the predictions
fail.

What is borrowed is stated explicitly. Goal macros, tunnel macros and PI-corrals originate
in Rolling Stone @junghanns2001, room abstractions in @botea2002, and backward (pull)
search is standard in Sokoban solvers. The goal-room macro here is a restricted
reimplementation of the room idea. What is new here is the switch penalty as a priority
term, the quantitative diagnosis, the formal treatment of each change inside one solver,
and a measurement of what each change buys and costs at benchmark scale.

// =====================================================================================
= Problem formalisation <sec:formal>
// =====================================================================================

#definition(name: "Instance")[
  A Sokoban instance is a tuple $cal(I) = (G, Gamma, B_0, p_0)$ where $G = (V, E)$ is the
  4-connected grid graph on the floor cells $V$, $Gamma subset.eq V$ is the goal set,
  $B_0 subset V$ with $|B_0| = |Gamma| = n$ is the initial box set, and
  $p_0 in V without B_0$ the initial player cell. For a direction
  $delta in Delta = {arrow.t, arrow.b, arrow.l, arrow.r}$ $v + delta$ denotes the
  neighbouring cell.
]

#definition(name: "Push state")[
  A state $s = (B, p)$ has player region $R(s)$, the connected component of $p$ in
  $G[V without B]$. A push of box $b in B$ in direction $delta$ is legal iff
  $b - delta in R(s)$ and $b + delta in V without B$; it yields
  $s' = (B without {b} union {b + delta}, b)$. A state is solved iff $B = Gamma$.
]

Because walking is free in push-optimal search, the solver identifies states with equal
boxes and equal player regions. It stores the canonical representative
$kappa(s) = min R(s)$ in row-major order and hashes $(B, kappa(s))$ with Zobrist keys
@zobrist1970 $z(s) = ⨁_(b in B) z_"box" (b) ⊕ z_"pl" (kappa(s))$.

#definition(name: "Push distance, dead squares")[
  Let $d(v, gamma)$ be the least number of pushes that move a lone box from $v$ to
  $gamma$ on the empty board, computed by backward breadth-first search over pulls. The
  dead squares are $D = {v in V : d(v, gamma) = infinity med forall gamma in Gamma}$. A box
  on $D$ can never reach a goal, so any state with $B inter D eq.not emptyset$ is dead.
]

The default lower bound is the assignment heuristic
$
  h(s) = min_(pi : B -> Gamma "bijective") sum_(b in B) d(b, pi(b)),
$ <eq:h>
computed with the Hungarian method @kuhn1955. It ignores box interactions and is
therefore a lower bound on the remaining pushes. If no finite-cost bijection exists the
state is dead. One thread instead uses a greedy assignment; this is taken up in
@sec:matching.

Two per-thread additions make the value the search actually uses larger than @eq:h. The
_linear-conflict_ term looks at the boxes that share a wall-free row or column with their
assigned goals: boxes cannot pass each other along a line, so every box outside a longest
subsequence whose goals are in the same order as the boxes adds 2. The _funnel_ term adds,
for every empty goal $gamma$, a depth $2 delta(gamma) + omega(gamma)$, where $delta(gamma)$
is the distance in cells from $gamma$ to the nearest non-goal floor cell and
$omega(gamma)$ the number of walls adjacent to $gamma$. An empty goal at the back of a
goal area therefore costs more than one at its mouth, and the search fills goal areas from
the back. With the funnel, $h$ is no longer a lower bound; it is a ranking function, which
is all a satisficing search needs. Threads with the funnel also run an _empty-goal freeze
test_, which rejects a push that freezes boxes around a goal no box can enter any more.
@sec:history explains where these came from.

== Search skeleton and priority

Each thread runs greedy best-first search over push states with a 4-ary min-heap keyed by
$
  phi(s) = w_h h(s) + w_sigma sigma(s) + w_g g(s) + w_omega omega(s) + epsilon,
$ <eq:phi>
where $g$ is the number of pushes from the root, $sigma$ the number of _box switches_ on
the path (pushes of a box different from the previously pushed one), $omega$ a walking
distance, and $epsilon in [0, 100)$ a random tie-breaker. The 64-bit key is
$phi dot 2^16 + (2^16 - 1 - min(g, 2^16 - 1))$, so deeper states win exact ties. States
live in a pool of $10^7$ fixed-size records; when it fills, the thread stops.

Dividing @eq:phi by $w_h$ exposes the trade-off that matters below:
$
  phi(s) / w_h = h(s) + (w_sigma / w_h) sigma(s) + lambda g(s) + dots,
  quad lambda = w_g / w_h .
$ <eq:lambda>
With $lambda = 0$ the order is pure greedy search on $h$; with $lambda = 1$ it is the
A\* order @hart1968; as $lambda -> infinity$ it is uniform-cost search on $g$, that is,
breadth-first search in pushes. Weighted A\* @pohl1970 corresponds to $lambda in (0, 1)$.

== The box-switch penalty <sec:switch>

#definition(name: "Switch count")[
  For a path $s_0 -> s_1 -> dots -> s_k$ in which push $i$ moves box $b_i$ (identified by
  the cell it leaves, since boxes are indistinguishable), let
  $sigma(s_k) = |{ i in {2, dots, k} : "src"(b_i) eq.not "dst"(b_(i-1)) }|$, the number of
  pushes that do not continue with the box moved by the previous push. It is maintained
  incrementally: a child inherits $sigma$ and adds one if its push starts somewhere other
  than its parent's last destination.
]

The term $w_sigma sigma(s)$ in @eq:phi makes the search prefer, among states of similar
heuristic value, those reached by paths that finish one box's trip before starting another.
Three properties separate it from inertia tie-breaking @zubaran2011 @pereira2016:

- *Scope.* Inertia compares only states with equal primary key ($f$ in A\*). The switch
  penalty enters the primary key, so a state with a lower $h$ can lose to one with fewer
  switches.
- *Memory.* Inertia looks at one step: whether the last push continued the previous box.
  $sigma$ accumulates over the whole path, so two states with the same last move can
  differ by many switches.
- *Price.* Inertia never trades against $h$. The penalty does, at an exchange rate of
  $r = w_sigma slash w_h$ heuristic units per switch, and that rate must be chosen
  (@sec:exchange).

The penalty is not admissible and is not meant to be: the solver searches for any
solution, not a short one. Its role is to remove interleavings. Greedy search on $h$ alone
happily advances box A one cell, then box B, then A again, and every such interleaving is a
distinct path to similar states; with $w_sigma > 0$ the interleaved paths sort behind the
sequential ones. The cost is that some levels need interleaving (a box parked halfway
while another passes), and on those a high $r$ delays the search. This is why the
portfolio runs both switch-averse threads ($r = 2$ or $2.5$) and a zero-switch thread
($r = 0$).

== The baseline portfolio

The four baseline threads differ in their parameters and pruning (@tab:threads-old).
Threads 0 and 1 use stagnation recovery: after 25 000 pops without improving the best
$h$, and then every 4 000 pops, they re-key the whole queue with an increased depth
weight. Thread 3 instead sinks the top 16 384 entries, and thread 2 never intervenes.

#figure(
  table(
    columns: 6,
    align: (left, left, right, right, right, left),
    table.hline(),
    [*Thread*], [*Role*], [$w_sigma slash w_h$], [$w_g slash w_h$], [*Matching*], [*Recovery*],
    table.hline(stroke: 0.4pt),
    [T0], [funnel sprinter (1-5, 1-9)], [2.0], [0.05], [Hungarian], [full re-key],
    [T1], [traffic juggler], [1.5], [0.05], [Hungarian], [full re-key],
    [T2], [classic baseline (1-7)], [2.5], [0], [Hungarian], [none],
    [T3], [zero-switch diver (C-10)], [0], [0], [greedy], [pocket sink],
    table.hline(),
  ),
  caption: [Baseline thread configurations (31 October 2026). All use $w_h = 10^6$. T1 also
    adds a walking term $w_omega = 10^4$. T0 and T1 use the funnel, the empty-goal freeze
    test and the full corral scan; T2 and T3 do not. Role names cite the levels each thread
    was built for (@sec:history).],
) <tab:threads-old>

// =====================================================================================
= Development history <sec:history>
// =====================================================================================

The baseline of @tab:threads-old is not a starting point. It is the state of the solver on
31 October 2026, after about three months of tracked development and an untracked
prehistory. This section records that path, for two reasons. Several results in later
sections correct or reverse earlier decisions, and they read differently once the earlier
decision is known. And the baseline's own behaviour on level 11 (@sec:failure) was first
seen, and patched without being understood, on other levels.

The sources are the repository's commit history, two benchmark files kept from July
(`bench/bench_1.csv`, `bench/bench2.csv`) and the audit logs of 30 and 31 October in
`docs/`. All numbers in this section come from those records. They were measured on
earlier builds, with a 15 s limit where a limit applies and on hardware the records do not
name, so they are not comparable with @sec:bench. @tab:timeline lists the milestones.

#figure(
  table(
    columns: 2,
    align: (left, left),
    table.hline(),
    [*Date (2026)*], [*Change*],
    table.hline(stroke: 0.4pt),
    [before 21 Jul], [Java course project; rewrite in C++, then C, then procedural C++ (untracked)],
    [21 Jul], [first tracked version: one 1 743-line source file, four racing threads, Java GUI over JNI; first full benchmark],
    [23 Jul], [Hungarian matching replaces greedy matching with 2-opt; first tunnel macro],
    [28 Jul], [precomputed tunnel macros added and withdrawn the same day; second full benchmark],
    [1 Aug], [low-level optimisation of the hot loop],
    [10 Sep], [tunnel macros removed],
    [8–15 Oct], [switch penalty set to zero, then to 2.5 h-units "from trial and error"; linear conflict generalised],
    [18–20 Oct], [per-thread configuration file (`flags.h`); JNI bridge dropped],
    [23–26 Oct], [tracing build and browser search-tree explorer replace the Java GUI],
    [30–31 Oct], [audits: corral, freeze, matching and recovery bugs fixed; funnel, pocket sink and per-thread pruning added. This is the baseline of @tab:threads-old],
    [1 Nov], [solver split into modules; bounded recovery, goal-room and delivery macros (@sec:recovery to @sec:delivery); terminal UI],
    [4 Nov], [backward thread (@sec:backward)],
    [6 Nov], [per-thread benchmark records and the analysis project; full benchmark of @sec:bench],
    [13 Nov], [cache-layout and allocation optimisations, after the benchmark and not reflected in it],
    table.hline(),
  ),
  caption: [Milestones in the repository history.],
) <tab:timeline>

== Origins

SokoBot began as a Java solver written for a university course. Two things pushed it out of
Java: the garbage collector, which dominated run time at this scale of search, and the
overhead of object-per-state designs. The portfolio idea predates the native code. The
first plan for using several cores was one thread per push direction; a friend
objected that merging states and stitching solutions across threads would cost more than it
saved, and proposed racing complete searches with different priority functions instead. The
informal motivation was the no-free-lunch theorem @wolpert1997: open levels and corridor
levels reward opposite biases, so no single weighting should be expected to win on both.
Algorithm portfolios @gomes2001 are the established form of that argument.

The native solver went through three forms. A first C++ version used standard containers and
paid for their hidden allocations. A pure C version removed them: it stopped storing whole
boards, packed states into fixed-size records and kept all search memory in three flat
buffers (state pool, queue, hash table). Portable threading in C was the weak point, so the
code returned to C++ for `std::thread` and `std::atomic` only, keeping the C memory layout.
That style, with no allocation, virtual call or `std::function` in the expansion loop,
survives in the current code.

== The first tracked solver

The first commit (21 July) already has the skeleton of @sec:formal: push-level best-first
search, a pool of $10^7$ states, a 4-ary heap, a transposition table of 32-bit entries (an
8-bit tag and a 24-bit pool index), Zobrist hashing, and 2×2, freeze, PI-corral and Hall
feasibility pruning. It differs from the baseline in three ways.

_Heuristic._ Boxes were matched to goals greedily, nearest goal first, followed by a
bounded 2-opt pass and a linear-conflict term for adjacent boxes whose goals are swapped.
Two days later the exact Hungarian matching of @eq:h replaced the greedy pass and the
2-opt repair. The greedy matching survives in thread T3, where @sec:matching finds the
failure that the 2-opt pass had been hiding.

_Priority._ The key had the same four terms as @eq:phi, but with one global switch weight,
$w_sigma slash w_h = 0.5$, and eight hard-coded reweightings of which four ran. The four
were chosen to spread the switch weight, with exchange rates $r = 0$, $0.5$, $2$ and $5$:
on a 153-level sample that set solved 133 levels against 129 for sets that kept two
threads at the same rate. So the idea that the portfolio should bracket $r$ rather than
pick it is as old as the repository. What changed later is the centre of the bracket
(@sec:exchange).

_Interface._ The solver was a JNI library behind a Java Swing window. This matters only
because it made the search opaque: the first benchmark recorded status, solution length,
node count and time per level, and nothing about which thread won or why a run ended.

That benchmark covers the same 2 746 levels as @sec:bench. With a 15 s limit the first
tracked solver solved 2 471 of them (90.0%), timed out on 256 and ended without a solution
on 19.

== Tunnel macros: added, removed, restored

A tunnel macro pushes a box that enters a one-wide corridor straight to the corridor's far
end as a single transition. It was the first macro in the solver and went through four
states: an inline version on 23 July; a precomputed version on 28 July, with a guard
against pinning a box on a non-goal cell at a dead end, withdrawn the same day as flawed;
deletion on 10 September; and a return in October as a per-thread flag, with a comment that
the macro is incomplete and should stay off in at least one thread. By 30 October it was on
in all four.

The two July benchmarks bracket the first two states. The second, committed on 28 July,
solved 2 444 levels against 2 471 in the first: 45 levels gained and 72 lost. More telling
than the totals is how runs ended. Runs that stopped without a solution rose from 19 to 62,
and 57 of the 62 stopped in under a second, so they were not search limits: every thread
had run out of states. Of those 62 levels, 49 had been solved a week earlier, and the
line-up of @sec:bench solves 59. Boxxle 1 level 4 is the clearest case: solved in 176
expansions on 21 July, declared unsolvable after 44 on 28 July. The build of 28 July
differs from the first in the matching and in the macros, so the totals alone cannot assign
the loss, but the first benchmark already has the symptom on a smaller scale (10 runs
ending in under a second, 9 of them on levels solved today). Some pruning rule was unsound
from the first commit, and the macro made it worse.

@sec:bench closes this loop. Its per-thread records show threads running out of states on
solvable levels, and on 15-48, 19-46 and 3-94 the cause is the tunnel macro's refusal to
make a single push. In July the symptom was the same, but with one status per level it
could not be attributed to a thread, let alone to a rule.

== The switch penalty before the exchange-rate study

The penalty is present in the first commit, with a comment that states its purpose and its
risk: it favours finishing a run of pushes on one box, and it is "exactly the wrong bias"
for levels that need interleaving. Its weight then moved four times.

+ July: $r = 0.5$ globally, spread to ${0, 0.5, 2, 5}$ across threads.
+ 8 October: set to zero in all threads.
+ 15 October: restored with a base rate of $2.5$, by the commit's own account from trial
  and error. The
  per-thread configuration file of 18 October keeps $r = 2.5$ in two threads, and that
  value is still T2's.
+ 30–31 October: $r = 2$ and $1.5$ in the two recovery threads, decaying during stagnation
  to a floor of $0.5$.

The last step rests on a conclusion that this report reverses. The audit of 31 October
examined level 11 and saw states at depth 86 with $h = 183$ ranked behind states at depth 30
with $h = 255$. It named two causes: a switch penalty of two h-units outweighing the one
unit a productive push earns, and a recovery schedule that had raised the depth weight to
122.7 h-units per push. It acted on the first. Setting the penalty to zero made the search
thrash between boxes and stall at $h = 190$, so the audit settled on a decay towards
$0.5$ h-units and recorded that as the right rate for congested levels. @sec:failure shows
that the second cause was the operative one (its closed-form estimate of the depth weight,
115, agrees with the logged 122.7), and @sec:results shows that once recovery is bounded
and the room macro exists, holding $r = 2$ fixed is 2.9 times better than letting it decay.

The audit of 30 October found a second effect that returns later. A push that puts a box on
a goal is necessarily followed by a switch, which costs $r = 2.5$ h-units, while pushing
the placed box one cell further costs one. The search therefore preferred to push finished
boxes past their goals. Making the switch after a goal arrival free doubled the speed on
C-10 but broke levels with a narrow goal-room entrance: with arrival free, the search parked
boxes on the entrance goal instead of packing the room from the back. The fix adopted then
was the funnel term of @sec:formal, which rewards deep goals through $h$. The
forgiveness rules of @sec:switch-accounting are the same idea made safe by the room macro,
which fixes the packing order so that a free switch can no longer block the entrance.

== Instrumentation

Every diagnosis in this report depends on a tracing build that did not exist before
23 October. Compiled with `SOKO_TRACE`, the solver emits one event per pop, per generated
child and per pruned push, with the prune reason, and times each phase of an expansion. The
trace runs one thread configuration at a time, since the race is not reproducible. Three
exports are used below: a depth profile (states generated, expanded, queued and dropped per
depth), a progress curve (best $h$, depth and elapsed time against expansion number) and a
summary with time per phase and prunes per reason. A browser explorer, and later a terminal
one, draw the search tree from the same trace. Configuration fields can be overridden per
run without rebuilding (Appendix C), which is how the sweeps of @sec:results were run.

The first use of per-thread records was to explain why thread 0 won almost every level.
There were two causes, neither of them algorithmic merit. Thread 0 started first and used a
cheap Manhattan distance, so it won trivial levels on latency. And the incremental
heuristic update re-matched the moved box greedily on seven of every eight pushes,
overwriting the Hungarian assignment, so the threads configured for exact matching ran
greedy matching 87.5% of the time and differed less than their configurations said.

== The October audits

The audits fixed four defects that had limited coverage since July. They are listed because
each is a soundness or completeness bug in a rule the later sections build on.

- *Corral scan stopped at the first corral.* A state with a live corral and a dead one was
  not pruned. On level 5 of _Original & Extra_ the search filled the pool after 8 759 668
  expansions with 6 057 corral deadlocks found. Scanning all corrals found 1 009 272 and
  solved the level in 624 000 expansions (1.63 s).
- *Freeze marks leaked.* The recursive freeze test marks boxes to detect cycles and did not
  clear the mark when a box turned out to be movable, so later tests in the same expansion
  treated it as frozen. Level 7 was declared unsolvable. The corral test had a related
  error: it rejected inward pushes using a dynamic 2×2 test on boxes the player could later
  move. It was restricted to static dead squares, the restriction that the dead-target lemma
  of @sec:corral relies on.
- *Stagnation was hidden.* The stagnation counter was reset whenever a popped key beat the
  best key since the last rebuild, which every fresh dive does. Resetting only on a new
  best $h$, together with a fix to the cut-vertex reachability test, took level 9 from
  7 919 899 expansions to 3 326 687.
- *A per-push graph search.* A reverse-pull reachability test for empty goals raised the
  time spent on push checks from 0.37 s to 11.13 s on one run and was reverted. The rule
  kept from this is that a filter applied to every candidate push must cost $O(1)$; the
  delivery macro of @sec:delivery breaks it deliberately and has to be rationed for that
  reason.

The recovery schedule was also patched twice in these two days, and both patches are in the
baseline. On level 40 the original schedule, which added $1.5 w_h$ per push of depth to
every queued state at each trigger, had reached 60 h-units per push after 40 triggers and
produced a bulge of 300 000 states at depth 35. The response was the pocket sink of
T3, which penalises only the 16 384 best entries. On level 9 the remaining threads needed
132 consecutive rebuilds to surface one state at depth 23, and the response was to charge
new states the cumulative rate and old states only the latest increment, which took the
level to 2 353 006 expansions. Both patches treat a symptom. Neither bounds the depth
weight, and on level 11 the same schedule fails in the way @sec:failure quantifies.

Finally, the audits replaced global feature switches with per-thread ones, because the test
levels wanted incompatible rules: levels 5 and 9 need the funnel, the empty-goal freeze test
and the full corral scan; level 6 needs a lower switch weight and a walking term; level 7
needs none of the new pruning; C-10 and level 40 need zero switch cost or the pocket sink.
One thread was built for each group. That is the origin of the four roles in
@tab:threads-old, and the reason their names cite levels.

// =====================================================================================
= Failure analysis on level 11 <sec:failure>
// =====================================================================================

#figure(
  grid(columns: 2, column-gutter: 1.2em, align: bottom,
    board(map-1-11, room: room-1-11, gate: (9, 4)),
    board(trap-1-11, corral: ((1, 11), (1, 12), (2, 11), (2, 12), (3, 12), (3, 10)),
          hot: ((3, 11), (4, 11), (3, 12))),
  ),
  caption: [Left: level 11 with the goal room shaded and its gate cell marked G. Boxes are orange,
    goals blue. Right: a dead state the baseline corral test could not prove dead
    (@sec:corral). The two outlined boxes block a one-wide gap; the upper box's only
    remaining push lands on a dead square (×) and must be made from a cell the player
    cannot reach.],
) <fig:board>

The baseline T0 run stops after 5 896 802 expansions with the pool full
(9 999 998 states), unsolved, after 56.2 s. Three observations follow from the telemetry.

#observation(name: "queue maintenance dominates")[
  32.9 s (58.5%) of the run is spent in pops and stagnation re-keying, against 22 s for all
  per-state work (heuristic 7.0 s, player flood fill 4.3 s, hashing 3.1 s, corral search
  2.5 s). The cost per expansion rises from 3.7 µs over the first million expansions to
  13.7 µs over the last million (@fig:baseline).
]

#observation(name: "progress stops early")[
  The best $h$ reaches 156 at expansion 2 859 948 and never improves over the remaining
  3.04 million expansions.
]

#observation(name: "the search turns shallow")[
  85% of all generated states lie at depth $<= 60$ (median 41), while the best states lie
  at depth 95–135. Of the 58 817 states still queued at depth 95–135 when the pool fills,
  none is popped after the stall.
]

#figure(
  image("figures/baseline-telemetry.svg", width: 62%),
  caption: [Baseline T0 on level 11. Top: wall-clock cost per expansion, averaged over
    windows of about 295 000 expansions. Bottom: best heuristic value found so far.],
) <fig:baseline>

== Why recovery degenerates

After a non-improving rebuild the baseline adds $1.5 dot 10^6$ (first rebuild) and then
$1.5 dot 10^5$ per further rebuild to the depth weight, resetting only when the best $h$
improves. After $k >= 1$ consecutive non-improving rebuilds, therefore,
$
  lambda_k approx (w_g + 1.5 dot 10^6 + 1.5 dot 10^5 (k - 1)) / w_h
  = 1.55 + 0.15 (k - 1).
$ <eq:lambda-k>
The stall lasts about $3.04 dot 10^6 slash 4000 approx 760$ rebuilds, so
$lambda_760 approx 115$. (This ignores the extra constant the baseline adds to states
created since the previous rebuild, which only strengthens the effect.)

#proposition(name: "breadth-first degeneration")[
  Let $s, t$ be queued states with $g(s) < g(t)$. Under @eq:lambda, $s$ is popped before
  $t$ whenever
  $
    lambda > (h(s) - h(t) + (w_sigma slash w_h)(sigma(s) - sigma(t))) / (g(t) - g(s)).
  $
]
#proof[Rearrange $phi(s) < phi(t)$ using @eq:lambda; the tie-breaker is below one unit.]

At the stall the frontier's $h$ ranges over roughly $[156, 272]$, a spread of about 116
units, and a one-push depth gap is worth $lambda approx 115$ units. A state is therefore
beaten by almost any state one push shallower: the queue is ordered essentially by $g$,
which matches the observed collapse of pop depth and the stranded deep frontier.

== Why recovery is expensive

Each rebuild re-inserts all $n$ queued entries, costing $Theta(n log_4 n)$, and occurs
every 4 000 pops. The amortised cost per pop is
$
  C_"base" (n) = (n log_4 n) / 4000 ,
$ <eq:cost-base>
which for the final queue of $n approx 4.1 dot 10^6$ is about $1.1 dot 10^4$ heap steps
per pop, three orders of magnitude above the $O(log n)$ cost of a pop itself. This
explains the first observation (queue maintenance dominates) and the growth in
@fig:baseline.

// =====================================================================================
= Methods
// =====================================================================================

== Bounded recovery <sec:recovery>

The stagnation trigger is kept, but both the schedule and the rebuild change.

*Schedule.* Let $rho$ be the extra depth weight. At each rebuild, $rho <- Delta$ if the
best $h$ improved since the previous rebuild, and $rho <- min(rho + Delta, rho_max)$
otherwise, with $Delta = 1.25 dot 10^5$ and $rho_max = 10^6$. Hence
$
  lambda = (w_g + rho) / w_h <= 0.05 + 1 = 1.05 .
$
By @eq:lambda the order is then that of weighted A\* with heuristic weight
$1 slash lambda >= 0.95$: as a function of depth, it is never more breadth-first than
(essentially) A\*.

*Rebuild.* The new keys are recomputed from stored fields $(h, sigma, g)$ in place and the
heap is rebuilt bottom-up @floyd1964 in $O(n)$. Rebuilds occur every
$max(4000, n slash 16)$ pops, so
$
  C_"bounded" (n) = O(n) / max(4000, n slash 16) = O(16) quad "per pop",
$
independent of $n$, compared with @eq:cost-base.

#remark[
  Bounded recovery alone does _not_ solve level 11. It cut queue-maintenance time from
  32.9 s to 3.2 s on the same machine, but the run still filled the pool (6.2 M
  expansions) with best $h = 190$, never reaching depth 82. Bounding $lambda$ removes the
  breadth-first collapse but leaves a near-A\* search that cannot cover a level with about
  270 pushes in $10^7$ states. @sec:results shows that letting $lambda$ grow towards 2
  makes the search slower again (1.2 M expansions versus 0.23 M), and removing recovery
  entirely loses the instance.
]

== Goal-room decomposition <sec:room>

#definition(name: "Goal room")[
  A set $R subset V$ is a _goal room_ with gate $a in V without R$ and entry
  $e in R$ if:
  (a) $R$ is connected in $G$ and $a$ is the only cell outside $R$ adjacent to $R$,
  with $e$ its only neighbour in $R$;
  (b) $R inter B_0 = emptyset$ and $p_0 in.not R$;
  (c) the cell $a - (e - a)$ behind the gate is floor, so a box can be pushed from
  $a$ to $e$;
  (d) $|Gamma_R| >= 2$, where $Gamma_R = Gamma inter R$, $m = |Gamma_R|$.
]

The detector enumerates candidate gates, flood-fills each side, and keeps the candidate
with most goals, then fewest cells, subject to $|R| <= 256$. For level 11 it finds
$|R| = 19$ cells holding $m = 14$ goals (@fig:board, left).

#definition(name: "Packing order")[
  For $S subset.eq Gamma_R$, let $"feas"(S, gamma')$ hold iff, on the board restricted to
  $R union {a}$ with boxes exactly on $S$, a box at $e$ with the player at $a$
  can be pushed to $gamma'$ and the player can then return to $a$. A _packing order_
  is a sequence $pi = (pi_1, dots, pi_m)$ of the goals of $Gamma_R$ with
  $e in.not {pi_1, dots, pi_(m-1)}$ and $"feas"({pi_1, dots, pi_(k-1)}, pi_k)$ for all
  $k$.
]

The order is found by depth-first search over filled sets with a failure memo, trying
goals farthest from $e$ first. The recursion
$
  F(S) = [|S| = m] or (e in.not S and exists gamma' in Gamma_R without S :
    "feas"(S, gamma') and F(S union {gamma'}))
$
visits at most $2^m$ sets, each testing at most $m$ goals by a breadth-first search over
(box, player) pairs in $R$, so the worst case is $O(2^m m |R|^2)$. The condition
$e in.not S$ encodes that an entry goal can only be filled last.

*The macro transition.* Let $f(s) = |B inter R|$. In the search: (1) no box in $R$ is ever
pushed; (2) the only legal push into $R$ is $a -> e$, and only while $f(s) < m$;
(3) that push is replaced by a single transition placing the box on $pi_(f(s)+1)$, with
the player on $a$, and $g$ increased by the precomputed push count of that placement.

#proposition(name: "soundness")[
  Every path found with the macro expands into a legal sequence of pushes.
]
#proof[
  By (1)–(3), the boxes inside $R$ at any state are exactly $pi_1, dots, pi_(f(s))$ in
  that order of arrival. When the macro fires, the board inside $R union {a}$
  coincides with the board for which $"feas"({pi_1, dots, pi_(f(s))}, pi_(f(s)+1))$ was
  established, because cells outside $R union {a}$ are irrelevant to pushes inside the
  single-entrance room. The stored witness path is therefore legal, and the player ends
  at $a$, from which the next walk is planned. Path reconstruction replays exactly
  these witnesses.
]

#proposition(name: "interior abstraction")[
  Ignoring the player, the number of interior configurations the search can represent
  drops from $sum_(k=0)^m binom(|R|, k)$ to $m + 1$. For level 11 this is
  $sum_(k=0)^(14) binom(19, k) = 519 252$ versus 15.
]

#remark(name: "what the macro gives up")[
  Boxes are indistinguishable, so _any_ packing order serves equally well for filling $R$;
  the fixed order loses nothing on its own. Solutions are excluded only if they (i) use
  $R$ as temporary storage, pushing a box in and later out; (ii) require a box to stop on
  a non-goal cell of $R$; or (iii) need a room with several entrances, which the detector
  rejects. No level-11 solution needing these is known, but the macro is incomplete in
  general, and the solver relies on the portfolio for coverage.
]

The first detector required $e in.not Gamma$. Relaxing this to "$e$ is filled last" (the
condition in the packing-order definition) raised the share of generated goal-room
instances on which the macro activates from 26 to 44 out of 60.

== Delivery macro <sec:delivery>

The room macro handles boxes once they reach $e$. Delivering a box to $e$ still takes
dozens of pushes, and greedy search interleaves the partial trips of different boxes. With
$k$ boxes each part-way along, the search can represent every combination of their
progress, although the order of completion rarely matters.

#definition(name: "Delivery transition")[
  For box $b$ in state $s$, consider the graph on pairs $(c, delta)$: box on cell $c$,
  player adjacent on side $delta$. There is an edge $(c, delta) -> (c + delta', -delta')$
  if, with all other boxes fixed, the player can walk from $c + delta$ to $c - delta'$
  and $c + delta' in.not D$. A breadth-first search from the sides of $b$ reachable in
  $R(s)$ to any pair with $c + delta' = e$ yields a shortest push sequence. If one is
  found within $L$ expanded pairs, $s$ gets one extra child: $b$ pushed to $e$, which
  then fires the room macro.
]

#proposition[
  Delivery transitions do not change the set of reachable states or remove any
  transition. A state graph with them has the same solutions, with $g$ still counting
  real pushes.
]
#proof[Each delivery edge is the composition of legal pushes found by the inner search, and
  the ordinary pushes of $b$ remain available.]

The pair graph has at most $4|V|$ nodes, and each node costs one flood fill, so an attempt
costs $O(L |V|)$. Offering deliveries for every box made level 11 slower in wall-clock time
despite fewer expansions, so the solver limits attempts to the $k = 1$ outside box with
the smallest goal distance and to $L = 512$ pairs.

== Corral-deadlock refinements <sec:corral>

A _corral_ $C$ is a connected component of $G[V without B]$ other than $R(s)$; its
_barrier_ is the set of boxes adjacent to $C$. Following @junghanns2001, the baseline
declares $C$ a PI-corral when (P1) every legal push of a barrier box from $R(s)$ moves it
into $C$, and (P2) no barrier box can be pushed _into $C$_ from a cell outside $R(s)$
that the player might reach later. If $C$ still needs work (it holds an empty goal, or a
barrier box is off goal) and no push in (P1) is _valid_ (not immediately dead), the state
is declared dead.

The right panel of @fig:board shows why this missed the 570 000-expansion trap behind
the stall at $h = 53$: the upper box's only escape push lands on a dead square and starts
from a cell the player cannot reach, so (P2) fails and the corral is not PI. Three rules are
added.

#lemma(name: "dead targets")[
  In (P2), pushes whose target lies in $D$ may be ignored without making the test unsound.
]
#proof[A push onto $v in D$ produces a state with $B inter D eq.not emptyset$, which is dead
  (Definition 3). No solution contains it, so it cannot be the move that opens $C$.]

The first new rule is the dead-target lemma above. The other two rest on one argument.
Fix a state $s$ and a corral $C$ with barrier $Q$. Along any solution from $s$ that ever
moves a box of $Q$, let $t^*$ be the index of the _first_ push of a box of $Q$.

#lemma(name: "first exit")[
  On every step $t < t^*$, the cells of $Q$ are occupied exactly as in $s$. Consequently
  any property of a cell that depends only on walls, on $D$, and on the occupancy of
  $Q$ holds unchanged on all of $0, dots, t^*$.
]
#proof[By the minimality of $t^*$, no push before $t^*$ moves a box of $Q$, and pushes of
  other boxes cannot move boxes of $Q$. Walls and $D$ are static. Induction on $t$.]

*Stuck barrier.* A box $x in.not Q$ is treated as unable to open $C$ if each of its four
pushes either starts or ends on a wall or a box of $Q$, or ends in $D$.

#proposition(name: "stuck barrier, relative soundness")[
  If $x$ satisfies the stuck-barrier condition in $s$, then on any solution from $s$, $x$
  occupies its cell in $s$ at step $t^*$. Hence the first barrier push cannot need $x$ to
  have moved, and the rule adds no unsoundness to the corral test beyond that of (P2).
]
#proof[The condition depends only on walls, $D$ and $Q$, so by the first-exit lemma it
  holds at every $t <= t^*$. At each such step every push of $x$ is illegal or lands in
  $D$; a solution contains no push into $D$ (dead targets). So $x$ never moves before
  $t^*$, by induction on $t$.]

This argument and the next came out of the Lean 4 formalisation of the corral rules
(Appendix B); the proofs above are the pen-and-paper versions.
The word _relative_ matters: (P2) still only enumerates pushes whose target is empty in
$s$, and treats a target occupied by a non-barrier box as permanently blocked. That
assumption is inherited from the baseline and remains unproven.

*Barrier 2×2.* A push of barrier box $b$ onto a non-goal cell $v$ is also ignored in (P2)
if $v$ would complete a 2×2 square whose other three cells are walls or boxes of
$Q without {b}$.

#proposition(name: "barrier 2×2")[
  Ignoring such pushes does not make the test less sound than (P2).
]
#proof[Every box of the square is blocked on both axes by walls or barrier boxes, so none can
  move until some box of $Q$ moves (first-exit lemma); the boxes of the square are
  themselves in $Q$ or the pushed box. The square therefore freezes, with a box on the
  non-goal cell $v$: a dead state.]

*Scope of (P2).* The baseline enumerates only pushes _into_ $C$. With the new rules this
restriction becomes unsafe: on 2 of 447 generated instances the thread declared a
solvable initial state dead. The cause, diagnosed during the formalisation, is a corral
_without goals_. Such a corral needs work only because a barrier box is off goal, and that
box must leave $C$'s boundary, not enter $C$; restricting (P2) to inward pushes then
ignores exactly the pushes that matter. Restricting the optimistic scope to corrals that
contain a goal (_goal scope_) removes both false deadlocks, but on level 11 it also
removes the entire speed-up: the trap of @fig:board is a goal-less corral (@tab:corral).
The fast rule is fast precisely because it is unsound there.

#figure(
  table(
    columns: 4,
    align: (left, right, right, right),
    table.hline(),
    [*Variant on level 11*], [*Expansions*], [*Untraced time*], [*False deadlocks / 447*],
    table.hline(stroke: 0.4pt),
    [no new rule], [1 088 619], [—], [0],
    [\+ stuck barrier], [986 825], [—], [0],
    [\+ dead targets, sound scope], [536 529], [5.3 s], [0],
    [\+ dead targets, sound scope, barrier 2×2], [368 763], [—], [0],
    [\+ dead targets, optimistic scope], [224 350], [2.0–2.2 s], [2],
    [\+ dead targets, optimistic _goal_ scope], [535 338], [—], [0],
    [\+ dead targets, optimistic scope, barrier 2×2], [130 060], [—], [2],
    table.hline(),
  ),
  caption: [Corral refinements on level 11, all with room and delivery macros. Rows with
    five-seed measurements report the median (seed ranges are within ±2.5%). False deadlocks
    are counted over the 447 generated instances. The optimistic variants run only in the
    goal-room thread; other threads cover their false deadlocks.],
) <tab:corral>

== Exact-matching fallback <sec:matching>

Thread T3 uses a greedy assignment instead of @eq:h, and declares a state dead when the
greedy pass leaves a box unassigned.

#proposition[
  A greedy assignment can fail on an instance that has a finite-cost perfect matching.
]
#proof[Two boxes $b_1, b_2$ and goals $gamma_1, gamma_2$ with $d(b_1, gamma_1) = 1$,
  $d(b_1, gamma_2) = 5$, $d(b_2, gamma_1) = 3$, $d(b_2, gamma_2) = infinity$. Greedy takes
  $b_1 -> gamma_1$ (cost 1) and leaves $b_2$ unassigned, while $b_1 -> gamma_2$,
  $b_2 -> gamma_1$ is a matching of cost 8.]

T3 therefore refused to start on 4 solvable generated instances. On a greedy failure the
thread now reruns the exact Hungarian matching and declares a dead state only if that fails too.

== Switch accounting <sec:switch-accounting>

Two proposals change how $sigma$ is maintained rather than how it is weighted.

*Completion-switch forgiveness.* After a goal-room placement the placed box can never move
again, so the next push must switch boxes. The baseline still charges $w_sigma$ for it, so
a state with $k$ placed boxes carries about $k - 1$ forced switches, about $2(k - 1)$
heuristic units of penalty that grows with progress. _Room forgiveness_ does not count a
switch whose predecessor was a room placement. _Sink forgiveness_ (with the funnel) also
forgives the switch after a push onto a _sink goal_, a goal with no strictly deeper goal
adjacent.

*Switch-dominated replacement.* The transposition table keeps the first copy of a position
$(B, kappa)$. If a later copy has fewer switches and no greater $g$, it has a strictly better
key under @eq:phi, and the relation "fewer switches and $g$ no larger" is a strict partial
order, so replacing the stored copy can happen only finitely often per position. The
replacement queues the better copy. Optionally, a popped state whose slot was replaced is
skipped.

The arguments establish that each change improves or preserves individual keys, not that it
improves the search. Measured results (@sec:results) diverge: room forgiveness helps, sink
forgiveness is mixed, and replacement is harmful. Re-queued copies cost pool records and
re-expansions, and the stale copy's descendants keep their higher $sigma$.

== Backward search thread <sec:backward>

Every forward thread commits to an order of goal fillings, implicitly, through $h$. On
levels where only one order works the forward threads can spend their whole budget filling
goals that later block the rest. Level C-10 (@fig:c10) is the extreme case: six boxes, six
goals in a one-wide corridor, and a corridor that must be filled from the far end. The
matching heuristic rewards pushing any box onto any goal, so a greedy search fills the near
goals first and then cannot pass them. The best forward thread needs $8.9 dot 10^5$
expansions (@tab:showcase).

#figure(
  board(map-c10, size: 9pt),
  caption: [Level C-10. The six goals lie in a one-wide corridor that must be filled from the
    far (right) end. Forward greedy search rewards the near goals first.],
) <fig:c10>

Searching from the other end removes the ordering problem: starting from the solved
position, the first box to leave the corridor is necessarily the one nearest its mouth.

#definition(name: "Pull state space")[
  A pull of box $b$ in direction $delta$ is legal in $(B, p)$ iff the player can reach
  $b + delta$ and $b + 2 delta in V without B$; it moves $b$ to $b + delta$ and the player
  to $b + 2 delta$. The roots are $(Gamma, p)$ for one $p$ in each connected region of
  $G[V without Gamma]$. A pull state is a target iff its boxes are exactly $B_0$ and its
  player region contains $p_0$.
]

#proposition(name: "reversal")[
  If a sequence of pulls leads from a root to a target, the reversed sequence, with player
  walks inserted, is a legal push solution of $cal(I)$.
]
#proof[A pull of $b$ from $c$ to $c + delta$ with the player ending on $c + 2 delta$ is
  undone by a push of the box on $c + delta$ in direction $-delta$ from $c + 2 delta$: the
  start cell is free (the player stood there), the target $c$ was vacated by the pull. The
  target's player region contains $p_0$, so the first walk exists; every later walk ends on
  a cell the pull sequence shows to be in the player's region. Induction on the length.]

The thread uses the same machinery as the forward threads where it transfers. The
heuristic is a minimum-cost matching, now between current boxes and _start_ cells, with
cost $d^->(c, beta)$, the number of pushes that move a lone box from start $beta$ to $c$
(computed once by breadth-first search over pushes). A pulled box may only enter cells with
$d^->(c, beta) < infinity$ for some start $beta$; any other cell cannot lie on a forward
solution, so this pruning is sound. States are canonicalised by the minimum cell of the
player region exactly as in the forward search, so both directions hash identical positions
to identical Zobrist keys. Priority is $w_h h + epsilon$ with no switch, depth or recovery
term (@sec:direction shows why the switch term must be absent), and the thread stops at
$4 dot 10^6$ states. Forward-only rules (freeze, corral,
2×2, macros) are switched off, since their soundness arguments assume pushes.

The backward thread is not a replacement for forward search. On C-5, a small open level, it
is the slowest thread by a factor of ten (@tab:showcase), because pulling from a compact
goal cluster offers many symmetric choices. Its value is in the portfolio, which
@sec:bench measures.

// =====================================================================================
= The switch-penalty exchange rate <sec:exchange>
// =====================================================================================

The ratio $r = w_sigma slash w_h$ prices one box switch in heuristic units. Comparing the
key changes of sibling pushes from one state gives two clean conditions. Write a push as
$(Delta h, Delta sigma)$, with $Delta g = 1$ for every push, so $w_g$ cancels between
siblings.

#proposition(name: "open-board band")[
  If every productive push changes $h$ by exactly $-1$, the sibling order
  same box forward $(-1, 0)$ < same box neutral $(0, 0)$ < other box forward $(-1, 1)$ <
  same box backward $(+1, 0)$ holds iff $1 < r < 2$.
]
#proof[The key changes are $-w_h$, $0$, $w_sigma - w_h$ and $w_h$. The middle inequalities
  are $0 < w_sigma - w_h < w_h$.]

#proposition(name: "corridor-detour band")[
  The search prefers a one-push detour of the active box $(+1, 0)$ to abandoning it for
  another box's productive push $(-1, 1)$, while still rejecting a two-unit regression
  $(+2, 0)$, iff $2 <= r < 3$.
]
#proof[$w_h <= w_sigma - w_h$ gives $r >= 2$, and $w_sigma - w_h < 2 w_h$ gives $r < 3$.]

#proposition[
  The two bands are disjoint. No single exchange rate satisfies both conditions.
]

This is the cleanest available explanation of the portfolio data: the zero-switch thread T3
is the best thread on 101 open generated instances, while level 11, a congested level whose
deliveries need detours, wants $r >= 2$. A per-instance choice of $r$ is therefore better
motivated than any fixed value.

Three caveats limit the theory. (i) At $r = 2$ the detour and the abandonment tie exactly,
and the random tie-breaker decides. (ii) The analysis compares _siblings_. The queue
compares states with accumulated differences $(Delta h, Delta sigma) = (j, k)$, whose order
flips at every rational threshold $r = j slash k$, so sharp edges should blur. (iii) Real
pushes do not change $h$ in unit steps: the matching heuristic can jump, and macros move
several cells at once.

@tab:band measures $r$ on level 11 with five seeds each. Behaviour is flat for
$r in [2, 2.75]$ and degrades on both sides. The lower edge matches the theory: at
$r = 1.75$ the median rises 1.55-fold and one seed nearly triples. The upper edge appears
between 2.75 and 2.99, not at 3: $r = 2.99$ and $r = 3.0$ are indistinguishable, consistent
with caveat (ii). $r = 2.5$ was best by 3%, but the difference vanishes once the
refinements of @sec:corral and room forgiveness are added (123 289 versus 124 433), so
$r = 2$ is kept.

#figure(
  table(
    columns: 5,
    align: (left, right, right, right, left),
    table.hline(),
    [$r = w_sigma slash w_h$], [*median*], [*min*], [*max*], [*band*],
    table.hline(stroke: 0.4pt),
    [1.75], [348 237], [344 104], [655 331], [below],
    [2.00], [224 350], [223 637], [226 058], [corridor],
    [2.25], [224 225], [223 771], [225 054], [corridor],
    [2.50], [217 923], [215 354], [219 625], [corridor],
    [2.75], [224 558], [222 839], [225 112], [corridor],
    [2.99], [279 747], [279 280], [281 446], [corridor (theory)],
    [3.00], [280 024], [279 908], [281 949], [above],
    [3.25], [340 234], [338 569], [340 988], [above],
    table.hline(),
  ),
  caption: [Expansions of the goal-room thread on level 11 by exchange rate, five seeds each
    (before the barrier-2×2 rule and room forgiveness). Earlier single runs: $r = 1.5$ took
    682 760 and $r = 4$ took 364 832.],
) <tab:band>

#figure(
  image("figures/switch-sweep.svg", width: 58%),
  caption: [The data of @tab:band, one dot per seed; the line joins means. The shaded band is
    the corridor-detour prediction. The rise already appears at $r = 2.99$, inside the band.],
) <fig:switch-sweep>

*A depth-adjusted edge.* Comparing the one-push detour $(+1, 0)$ with a two-push sequence
that switches once and gains two units, $(-2, 1)$ with $Delta g = 2$, does not cancel $w_g$
and gives the edge $r < 3 - lambda$, with $lambda = (w_g + rho) slash w_h$. At the start of
the search $lambda = 0.05$, which places the edge at 2.95, between the observed 2.75 and
2.99. This refinement was proposed after the sweep, so its new predictions were tested
(@tab:edge, five seeds each, same configuration as @tab:band):

#figure(
  table(
    columns: 4,
    align: (left, left, right, left),
    table.hline(),
    [*Setting*], [*Prediction*], [*Median expansions*], [*Outcome*],
    table.hline(stroke: 0.4pt),
    [$lambda = 0.05$, $r = 2.85, 2.90, 2.93$], [plateau (≈ 225k)], [249k, 259k, 266k], [fails: gradual rise],
    [$lambda = 0.05$, $r = 2.97$], [degraded (≈ 280k)], [280k], [holds],
    [$lambda = 0$, $r = 2.99$], [plateau], [218k (r = 2: 324k)], [holds],
    [$lambda = 0.2$, $r = 2.75$], [plateau (edge at 2.8)], [460k (r = 2: 257k)], [fails],
    [$lambda$ held fixed ($rho_max = 0$)], [edges as above], [most runs unsolved in 40 s], [untestable],
    table.hline(),
  ),
  caption: [Tests of the depth-adjusted edge $r < 3 - lambda$ on level 11. The last row
    disables the growth of $rho$ so that $lambda$ stays constant (three seeds): the search then needs
    1.4–3.6 M expansions or fails, and higher $r$ does _better_.],
) <tab:edge>

The refinement predicts the endpoint of the rise at $lambda = 0.05$ and the recovery at
$lambda = 0$, but not the gradual rise before it or the behaviour at $lambda = 0.2$. Since
$rho$ grows during recovery, $3 - lambda$ ranges from 2.95 down to about 1.95 within one run,
so post-hoc readings of the edge can fit almost any data; with $lambda$ fixed, the search no
longer resembles the solver being modelled. The conclusion is that the sibling model explains the
lower edge and the existence of an upper edge, while the exact upper edge comes from an
interaction with recovery that no static exchange-rate model captures.

// =====================================================================================
= Portfolio analysis and redesign <sec:portfolio>
// =====================================================================================

Let $E_t (x)$ be thread $t$'s expansions on instance $x$ (or $infinity$ if it fails within
the cap). The race cost is $E^*(x) = min_t E_t (x)$. Define the _marginal contribution_
of $t$ as the factor by which the race gets more expensive without it:
$
  M_t (x) = (min_(u eq.not t) E_u (x)) / E^*(x) .
$
A thread with $M_t (x) = 1$ almost everywhere is redundant. @tab:contrib reports $M_t$ over
447 generated instances for the four baseline threads plus the goal-room thread.

#figure(
  table(
    columns: 5,
    align: (left, right, right, right, right),
    table.hline(),
    [*Thread*], [*Sole best on*], [*median $M_t$*], [*max $M_t$*], [*$M_t > 2$ on*],
    table.hline(stroke: 0.4pt),
    [T0], [11], [1.00], [1.3], [0],
    [T1], [43], [1.00], [2.5], [1],
    [T2], [23], [1.00], [1.9], [0],
    [T3], [101], [1.00], [7.0], [3],
    [goal-room thread], [62], [1.00], [16.3], [44],
    table.hline(),
  ),
  caption: [Marginal contribution on 447 generated instances (expansions, 300k-pop cap).
    No instance was solved by only one thread. T0 and T1 were within a factor of 1.5 of each
    other on 402 of the 447 instances.],
) <tab:contrib>

Two conclusions follow. First, T0 and T1 are near-duplicates on these instances: their
configurations differ only in $w_sigma$ (2.0 versus 1.5 h-units) and a small walking
term. Second, the goal-room thread is the only one with a large marginal contribution, and
it concentrates on goal-room instances. Since the target machine has four performance
cores, the goal-room thread replaces T1, and the room and delivery macros are added to
T0, T2 and T3 (T3 also gets the exact-matching fallback). Those macros only change
behaviour on instances with a detected room.

*Second redesign.* After the backward thread was added (@sec:backward), the portfolio had
five threads for a target of four cores. The marginal-contribution measure cannot be applied
to the backward thread on generated instances, which it rarely wins because they are easy
from both ends, so the choice was made on the full benchmark instead: each line-up was run
once on all 2 746 levels, and T2 (the classic baseline, $r = 2.5$, no recovery) was the
thread to drop. @sec:bench reports the result as a paired comparison of the line-ups
{T0, T1, T2, T3} and {T0, T1, T3, T4}. All five threads remain in the code, and the
line-up is chosen per machine through an environment variable.

// =====================================================================================
= Experiments <sec:results>
// =====================================================================================

*Setup.* Level 11 was reconstructed from the published level; its first three search
depths reproduce the original telemetry exactly (5 states at depth 1 with minimum $h$
271). Generated instances come from backward random walks of box pulls from the solved
state, which guarantees solvability: 387 _open_ instances (4–10 boxes, goals clustered
but unwalled, grids 12–17 × 10–13) and 60 _goal-room_ instances (3–7 boxes, a 3–4 × 3–4
walled room with a one-cell gap). Expansion counts are deterministic for a fixed build and
seed. Timings come from a 2-vCPU Linux container ("sandbox") unless marked "laptop" (an
Apple Silicon laptop, 4 performance cores). Every solution reported was replayed
by an independent simulator.

== Level 11

#figure(
  image("figures/ablation-bars.svg", width: 100%),
  caption: [Expansions on level 11 as changes are added cumulatively (single thread).
    Grey bars: unsolved, pool full. The "sound" and "optimistic" bars are alternatives; the
    last bar builds on "optimistic".],
) <fig:ablation>

#figure(
  table(
    columns: 5,
    align: (left, right, left, right, right),
    table.hline(),
    [*Configuration*], [*Expansions*], [*Result*], [*Sandbox*], [*Laptop*],
    table.hline(stroke: 0.4pt),
    [Baseline T0], [5 896 802], [pool full], [—], [56.2 s#super[t]],
    [Bounded recovery, decaying $w_sigma$], [6 205 940], [pool full], [—], [39.7 s#super[t]],
    [\+ goal-room macro], [4 513 652], [solved], [36.8 s], [25.6 s#super[t]],
    [\+ fixed $w_sigma = 2 w_h$], [1 569 753], [solved], [11.1 s], [—],
    [\+ delivery macro], [1 088 619], [solved], [—], [—],
    [\+ corral refinements (optimistic)], [227 331], [solved], [2.0–2.2 s], [—],
    [First 4-thread T1 (same rules)], [223 677], [solved], [1.9 s], [1.00 s],
    [\+ barrier 2×2, room-switch forgiveness], [124 551#super[m]], [solved], [1.55 s], [—],
    table.hline(),
  ),
  caption: [Level 11, single thread. #super[t] traced run (instrumentation on); other times
    are untraced. #super[m] mean of five tie-break seeds (range 122 648–125 990). The laptop's
    1.00 s was measured with threads {0, 1, 2, goal-room} before the last row's changes.],
) <tab:level11>

The early version shrank $w_sigma$ by 25% per non-improving rebuild down to a floor, a
design taken from the audit of 31 October, which had concluded that congested levels want
about half an h-unit per switch (@sec:history). On
level 11 this backfired: holding $w_sigma = 2 w_h$ fixed cut expansions 2.9-fold
(@tab:level11). With the room macro, finishing one box's trip before starting another is
exactly the behaviour a high switch penalty enforces. @fig:progress shows the
best-$h$ trajectories: the failing runs plateau, while the final configuration reaches the
goal after one long plateau that the corral refinements shortened.

#figure(
  image("figures/progress-ablation.svg", width: 100%),
  caption: [Best heuristic value against expansions on level 11 (log scale). × marks a run
    stopped by a full pool, ● a solved run.],
) <fig:progress>

#figure(
  image("figures/depth-profile.svg", width: 62%),
  caption: [States generated per depth. The baseline spends its pool on depths 20–60; the
    final thread's depth axis counts macro moves, so a solution has fewer levels.],
) <fig:depth>

== Parameter sensitivity

@tab:sweep perturbs one parameter at a time around the final goal-room thread (20 s cap,
an earlier build with delivery limits $k = 2$, $L = 4096$). The configuration sits near a
local optimum in each direction tested, and both stronger and absent recovery are clearly
worse.

#figure(
  table(
    columns: 3,
    align: (left, right, right),
    table.hline(),
    [*Change from final*], [*Expansions*], [*Traced time*],
    table.hline(stroke: 0.4pt),
    [none (reference)], [225 959], [2.8 s],
    [delivery $k = 1$, $L = 512$ (adopted)], [221 028], [2.6 s],
    [$w_g = 0.1 w_h$], [223 095], [2.8 s],
    [$w_g = 0$], [254 373], [3.1 s],
    [$w_sigma = 3 w_h$], [277 151], [3.5 s],
    [delivery macro off], [305 588], [3.4 s],
    [$w_sigma = 4 w_h$], [364 832], [4.5 s],
    [$rho_max = 0.5 w_h$], [649 556], [7.5 s],
    [$w_sigma = 1.5 w_h$], [682 760], [8.4 s],
    [baseline full re-key recovery], [683 332], [6.7 s],
    [entry-distance heuristic instead of @eq:h], [937 241], [9.1 s],
    [$rho_max = 2 w_h$], [1 202 209], [14.7 s],
    [goal-room funnel off], [> 1 740 060], [unsolved in 20 s],
    [no recovery], [> 1 906 758], [unsolved in 20 s],
    table.hline(),
  ),
  caption: [One-at-a-time sensitivity on level 11 (sandbox, traced).],
) <tab:sweep>

== Switch accounting <sec:switch-results>

@tab:accounting tests the proposals of @sec:switch-accounting on level 11 (five seeds,
against the then-default goal-room thread) and on the 447 generated instances for the
three threads with $w_sigma > 0$.

#figure(
  table(
    columns: 4,
    align: (left, right, left, left),
    table.hline(),
    [*Proposal*], [*Level 11 (mean)*], [*447 instances: changed / >1.5× worse / >1.5× better*], [*Verdict*],
    table.hline(stroke: 0.4pt),
    [none (reference)], [224 600], [—], [—],
    [room forgiveness], [207 200 (−8%)], [0 / 0 / 0 on T0, T1, T2], [adopted in T1],
    [sink forgiveness], [209 700 (−7%)], [T0: 171 / 16 / 11; T1: 174 / 17 / 11], [rejected],
    [switch-dominated replacement], [281 200 (+25%)], [T0: 71 / 5 / 2; T1: 72 / 6 / 1; T2: 59 / 5 / 8], [rejected],
    [#h(0.8em)with superseded-skip], [268 000 (+19%)], [T1: 72 / 5 / 1; T2: 58 / 5 / 8], [rejected],
    table.hline(),
  ),
  caption: [Switch-accounting proposals. No proposal lost or invalidated a generated instance.
    Sink forgiveness has no effect on T2 (funnel off) and swings widely on T0, from a 2 600×
    saving (81 062 → 31 expansions) to a 1 400× loss (43 → 61 066). Replacement raised T1's
    total expansions over the suite by 28% and T2's by 61%.],
) <tab:accounting>

Room forgiveness is the one change with no measured downside: it never alters a generated
instance (their rooms fill in a handful of expansions) and saves 8% on level 11. Sink
forgiveness looks similar on level 11 but is erratic on generated instances, so a
proof that it removes a "switch tax" says little about whether the search benefits.
Replacement is worse everywhere it matters: the better key is real, but the re-queued copy
costs a pool record and a re-expansion, and the stale copy's descendants remain queued.

== Generated instances

#figure(
  table(
    columns: 4,
    align: (left, right, right, right),
    table.hline(),
    [*Change*], [*Instances changed*], [*median ratio*], [*worse > 1.5×*],
    table.hline(stroke: 0.4pt),
    [Room macro added to T0 / T1 / T2 / T3], [44 each], [0.66 / 0.64 / 0.63 / 0.60], [0],
    [\+ delivery, on T0 / T2 / T3 (room set)], [44 each], [0.15 / 0.15 / 0.16], [0],
    [T3 exact-matching fallback (all 447)], [30], [0.98], [4],
    [Old race → new 4-thread race, open (387)], [—], [1.00], [1 (1.7×)],
    [Old race → new 4-thread race, room (60)], [—], [0.13], [0],
    table.hline(),
  ),
  caption: [Expansion ratios (new/old) on generated instances. The T3 fallback also gained
    4 instances and lost 1 within the cap. The new race solves all 447 instances, and every
    solution replays correctly.],
) <tab:generated>

== Backward search on two showcase levels

@tab:showcase runs each thread alone on C-10 and on C-5, a small open level. Expansion
counts are deterministic; times are single untraced runs on the 2-vCPU sandbox.

#figure(
  table(
    columns: 5,
    align: (left, right, right, right, right),
    table.hline(),
    [*Thread (alone)*], [*C-10 expansions*], [*C-10 time*], [*C-5 expansions*], [*C-5 time*],
    table.hline(stroke: 0.4pt),
    [T0 funnel sprinter], [1 468 052], [12.7 s], [15 862], [65 ms],
    [T1 goal-room packer], [1 456 126], [13.3 s], [8 709], [34 ms],
    [T3 zero-switch diver], [888 360], [6.6 s], [19 932], [64 ms],
    [T4 backward], [*7 813*], [*23 ms*], [91 204], [345 ms],
    [race {T0, T1, T3, T4}], [T4 wins], [56 ms], [T1 wins], [93 ms],
    table.hline(),
  ),
  caption: [Single-thread runs on C-10 and C-5 (sandbox). On C-10 the backward thread needs
    114× fewer expansions than the best forward thread; on C-5 it is the slowest. The race
    time exceeds the winner's solo time because four threads share two cores.],
) <tab:showcase>

== The switch penalty is direction-dependent <sec:direction>

The backward thread was first deployed with a moderate switch penalty,
$w_sigma = 0.75 w_h$, on the reasoning that what helps forward search should help backward
search. It does the opposite. @tab:direction runs the backward thread alone with and
without the penalty.

#figure(
  table(
    columns: 3,
    align: (left, right, right),
    table.hline(),
    [*Backward thread alone*], [*$w_sigma = 0$*], [*$w_sigma = 0.75 w_h$*],
    table.hline(stroke: 0.4pt),
    table.cell(colspan: 3)[_Level 1-13 (16 boxes), traced_],
    [expansions], [721 543], [6 797 000],
    [states stored], [1 686 381], [10 000 000 (limit)],
    [largest depth layer (states)], [89 419], [611 439],
    [expansions until $h <= 2$], [167 000], [6 677 000],
    [result], [*solved*, 3.4 s], [out of memory, 32.6 s],
    table.cell(colspan: 3)[_120 levels unsolved in the benchmark, 5 s limit, one core_],
    [levels solved], [*19*], [0],
    table.cell(colspan: 3)[_447 generated instances, 2 s limit_],
    [levels solved], [447], [447],
    [total time], [1.3 s], [0.4 s],
    table.hline(),
  ),
  caption: [Effect of the switch penalty on backward search. The 1-13 rows are traced runs
    on the laptop; the other rows are untraced sandbox runs.],
) <tab:direction>

With the penalty the search stores six times as many states, its widest depth layer is
seven times larger, and it needs forty times as many expansions to bring $h$ down to 2. It
then runs out of memory one step from the start position. Without the penalty it solves the
level in 3.4 s. On the 120 levels the benchmark line-up left unsolved, the unpenalised
thread alone solves 19 within 5 s on one core, and the penalised one none. The penalty is
slightly faster on easy generated instances (0.4 s against 1.3 s over 447 levels), which is
why the problem was not visible in the regression tests.

The mechanism follows from where the boxes are. A forward search starts with boxes scattered
over the level, and each can usually be taken to the goal area in one uninterrupted trip, so
penalising switches removes interleavings without excluding solutions. A backward search
starts with the boxes packed on the goals. To pull any box far, others must first be pulled a
short way out of its path, so productive pull sequences alternate between boxes by
necessity. The penalty charges every such alternation, ranks the productive sequences behind
long single-box excursions, and the search fills memory with the latter. The habit the
penalty encodes, finishing one box before starting the next, describes how boxes are
delivered, not how a packed room is unpacked.

This gives the switch penalty a property that inertia tie-breaking cannot have, since a
tie-breaker never changes which states are preferred across different $h$: its sign of
usefulness depends on the search direction. The practical rule is to penalise switches in
forward search only.

== Telemetry of the remaining failures <sec:residual>

The level-11 diagnosis (@sec:failure) predicts that a thread with full re-key recovery
spends its time on queue maintenance and stores mostly shallow states. Traces of three
further unsolved levels test how far that explanation reaches (@tab:residual).

#figure(
  table(
    columns: 6,
    align: (left, left, right, right, right, left),
    table.hline(),
    [*Level*], [*Thread*], [*Expansions*], [*Queue maint.*], [*Median depth*], [*Best $h$ and outcome*],
    table.hline(stroke: 0.4pt),
    [1-13], [T0, full re-key], [4 630 969], [47%], [52], [193 → 39; out of memory],
    [1-13], [T1, bounded#super[\*]], [2 104 746], [4%], [34], [235 → 90, flat for last $1.9 dot 10^6$; out of memory],
    [1-16], [T0, full re-key], [6 595 562], [62%], [48], [152 → 100, flat for last $4.3 dot 10^6$; out of memory],
    [19-26], [T0, full re-key], [4 632 847], [55%], [35], [252 → 136; out of memory],
    [19-26], [T4, backward], [7 660 436], [4%], [235], [89 from expansion 19 000 on; out of memory],
    [17-39], [T0, full re-key], [60 270], [2%], [38], [128 → 80; *ran out of states*],
    table.hline(),
  ),
  caption: [Traced single-thread runs on unsolved levels (laptop). Queue
    maintenance is the share of run time spent re-keying and re-heapifying the queue. Median
    depth is over all stored states. #super[\*]With the optimistic corral scope off, since
    it declares the initial position of 1-13 dead.],
) <tab:residual>

Three conclusions follow.

_Bounded recovery removes the cost, not the plateau._ On 1-13 the full re-key thread spends
47% of its time on queue maintenance (62% on 1-16, 55% on 19-26), as on level 11. Bounded
recovery cuts that to 4%. But the bounded thread is no deeper (median depth 34) and its
best $h$ stalls at 90. So the recovery pathology of @sec:failure generalises as a cost
problem, and @sec:recovery fixes it, but these levels also contain a plateau that no
recovery schedule in the solver crosses. On 1-13 and 1-16 the backward thread crosses it
instead: it solves 1-13 in $7.2 dot 10^5$ expansions (@tab:direction) and 1-16 in
$5.9 dot 10^6$ once its state limit is raised from $4 dot 10^6$ to $1.6 dot 10^7$ (34 s in
the sandbox, so outside a 10 s limit).

_Incomplete pruning is visible in a single trace._ On 17-39 the thread runs out of states
after 60 270 expansions with $h$ still at 80. The level is solvable, so the pruning removed
every remaining path; switching PI-corral pruning off removes the early exhaustion. This is
one of the events counted in @tab:dry.

_Some levels stall from both ends._ On 19-26 the forward thread creeps from $h = 252$ to
136 and the backward thread sits at $h = 89$ for 7.6 million expansions. Neither direction
alone finds a path, which motivates letting the two searches meet (@sec:proposal).

== Full benchmark <sec:bench>

*Setup.* The benchmark is the 2 746 levels shipped with the solver, drawn from public
collections (Microban, Boxxle, Sasquatch, Cosmac and others). Each line-up attempted every
level once with a 10 s wall-clock limit on an AMD Ryzen machine with 16 hardware threads
(CachyOS), so four solver threads ran without contention. In this run the backward thread
still carried the switch penalty $w_sigma = 0.75 w_h$ that @sec:direction shows to be
harmful, so the coverage reported for the new line-up is a lower bound for the design
described in @sec:backward. A level counts as solved only if
the returned move string, replayed from the start, ends with every box on a goal; no
returned solution failed this check. All statistics below are computed by the
`thesis_results` notebook in the repository's `analysis/` project.

The two line-ups compared are those of the second redesign (@sec:portfolio). The _old_
line-up is the portfolio after the first redesign, so it already contains the goal-room
thread and the macros; the comparison isolates the exchange of T2 for the backward thread
and does not measure the changes of @sec:recovery to @sec:corral at benchmark scale. The
only record of the solver before those changes is the July benchmark of @sec:history
(90.0% at 15 s on other hardware), which is not a controlled comparison.

Let $cal(M)$ be the set of levels, $S_c$ the levels solved by line-up $c$, and $t_c (m)$ the
wall-clock time of $c$ on level $m in S_c$.

*Coverage.* @tab:coverage reports $|S_c| slash |cal(M)|$ with 95% Wilson score intervals. The
intervals describe uncertainty about the solver's rate on levels like these; the benchmark
is a fixed set, not a sample of all Sokoban levels.

#figure(
  table(
    columns: 4,
    align: (left, left, right, right),
    table.hline(),
    [*Line-up*], [*Threads*], [*Solved*], [*Coverage [95% CI]*],
    table.hline(stroke: 0.4pt),
    [old], [T0 T1 T2 T3], [2 491], [90.7% [89.6, 91.7]],
    [new], [T0 T1 T3 T4], [2 626], [95.6% [94.8, 96.3]],
    [all five#super[\*]], [T0 T1 T2 T3 T4], [2 636], [96.0% [95.2, 96.7]],
    table.hline(),
  ),
  caption: [Coverage on 2 746 levels, 10 s limit. #super[\*]The five-thread line-up was run only
    on the 120 levels the new line-up missed; its total assumes it also solves every level the
    new line-up solves. Only 4 of those solves took more than 8 s, so this assumption is
    unlikely to be off by more than a few levels.],
) <tab:coverage>

*Paired coverage test.* Both line-ups ran on the same levels, so the comparison is paired.
Let $n_+ = |S_"new" without S_"old"| = 145$ (gained) and
$n_- = |S_"old" without S_"new"| = 10$ (lost). Under the null hypothesis of no difference
each discordant level is equally likely to fall either way, so
$n_+ ~ "Binomial"(n_+ + n_-, 1 slash 2)$. The exact two-sided McNemar test gives
$p = 7.7 dot 10^(-32)$. Of the 145 gained levels, the backward thread won 144; all 10 lost
levels had been won by T2, and the five-thread line-up recovers all 10.

*Paired speed test.* On the 2 481 levels both line-ups solve, the comparison uses
$ell(m) = ln t_"old" (m) - ln t_"new" (m)$, with times floored at 1 ms so timer noise on
trivial levels does not dominate. The geometric-mean speed-up $exp(macron(ell))$ is
1.31× (95% bootstrap CI 1.26–1.37, $10^4$ resamples), and a Wilcoxon signed-rank test
rejects a zero median ($p = 1.4 dot 10^(-48)$). The new line-up is faster on 37.3% of these
levels and slower on 19.5% (the rest tie at the millisecond floor). Total time on the shared
levels falls from 423 s to 181 s, because the largest savings are on the slowest levels.

#figure(
  image("figures/cactus.svg", width: 72%),
  caption: [Levels solved within a given time budget. The new line-up dominates at every
    budget: 2 063 against 1 928 levels within 10 ms, 2 554 against 2 388 within 1 s.],
) <fig:cactus>

*Thread roles.* @tab:wins counts winners. A win on a trivial level is decided by scheduling
noise, so _hard wins_, those taking at least 100 ms, are counted as well. In the new line-up every
thread keeps a niche of hard wins, and the backward thread has the most.

#figure(
  table(
    columns: 5,
    align: (left, right, right, right, right),
    table.hline(),
    [], table.cell(colspan: 2)[*old line-up*], table.cell(colspan: 2)[*new line-up*],
    [*Thread*], [*wins*], [*hard wins*], [*wins*], [*hard wins*],
    table.hline(stroke: 0.4pt),
    [T0 funnel sprinter], [915], [91], [653], [41],
    [T1 goal-room packer], [750], [125], [600], [73],
    [T2 classic baseline], [280], [43], [—], [—],
    [T3 zero-switch diver], [546], [58], [441], [22],
    [T4 backward], [—], [—], [932], [129],
    table.hline(),
  ),
  caption: [Winning thread per solved level. Hard wins take at least 100 ms.],
) <tab:wins>

*Difficulty.* Coverage falls with box count (@fig:boxes): the new line-up solves every level
with up to six boxes and 99.6% of those with seven or eight, but 90.2% of those with 11–14
and 73.7% of those with 15–20. Among solved levels, time correlates with box count
(Spearman rank correlation 0.77) and with floor size (0.69). The gains concentrate where the
old line-up was weakest: +9.0 points at 11–14 boxes and +10.1 at 15–20. By collection, the
largest gains are in Boxxle 2 (80 → 92 of 104), Sasquatch (17 → 25 of 31) and Yoshio
Murase's HandMade (46 → 53 of 54).

#figure(
  image("figures/coverage-boxes.svg", width: 62%),
  caption: [Coverage by number of boxes with 95% Wilson intervals.],
) <fig:boxes>

*Where the time goes.* Solve times are heavy-tailed. The median solved level takes 1.2 ms
and the 90th percentile 101 ms, but the slowest 1% of solved levels (26) account for 47.7%
of the 308 s total, and the slowest 10% for 95.5%. Average-time comparisons are therefore
dominated by a few levels, which is why the paired log-time test above is reported.

*Failure taxonomy.* Each unsolved level is classified by how its threads ended. Of the 120
levels the new line-up misses, 119 are _out of time_ (some thread was still searching at
10 s) and one is _out of memory_. No unsolved level is one where every thread ran out of
states. Individual threads do run out of states, though, and when that happens on a level
another configuration solves, it is direct evidence of incomplete pruning (@tab:dry).

#figure(
  table(
    columns: 3,
    align: (left, right, right),
    table.hline(),
    [*Thread*], [*old line-up*], [*new line-up*],
    table.hline(stroke: 0.4pt),
    [T0], [59], [38],
    [T1], [135], [114],
    [T2], [59], [—],
    [T3], [60], [36],
    [T4], [—], [0],
    table.hline(),
  ),
  caption: [Levels on which a thread ran out of states (or refused to start) although some
    configuration solved the level. Every such level is a level where that thread's pruning
    removed all solutions.],
) <tab:dry>

The backward thread never runs out of states on a solvable level, as expected from its
sound pruning. T1, the thread with the optimistic corral scope, does so on 114 levels, the
benchmark-scale counterpart of the 2 false deadlocks in 447 generated instances
(@tab:corral). Inspecting individual levels shows three sources. T1's optimistic corral
rule declares the _initial_ position dead on 19 of the 120 unsolved levels. Tunnel macros, which always push a
box completely through a one-wide tunnel, remove every solution on some levels; allowing
the single push as well fixes 15-48 and 19-46 and solves 3-94. The goal-room packing order
excludes the solutions found on 16-30, which T0 solves once the macro is off (with the
single-push tunnel change). Runs that end without a solution on solvable levels already
appear in the benchmarks of July 2026, where they could not be attributed for lack of
per-thread records (@sec:history). The portfolio
absorbs most of this, since another thread usually solves the level, but each such failure
costs a thread for the rest of the run.

// =====================================================================================
= Limitations
// =====================================================================================

- *One run per line-up.* Thread scheduling makes the race non-deterministic, so levels
  decided within a few milliseconds can change winner between runs, and levels solved near
  the limit can flip. Only 4 of the new line-up's solves took more than 8 s, and the paired
  tests have margins far beyond what a handful of flips could change, but winner counts on
  easy levels should be read with the hard-win column.
- *One machine, one limit.* Coverage at a wall-clock limit depends on hardware. The
  benchmark ran on 16 hardware threads, so four solver threads did not compete for cores;
  on a four-core laptop the same line-up would be slower. Expansion counts, used for the
  level-11 and generated-instance studies, are hardware-independent.
- *Backward thread configuration.* The benchmark ran the backward thread with the switch
  penalty of 0.75; the direction result of @sec:direction comes from separate single-thread
  runs (120 levels at 5 s, and traces). The line-up with the corrected thread has not been
  rerun on the full benchmark, so its coverage is not reported.
- *Traces from another machine.* The traces of @tab:direction and @tab:residual were
  recorded on the laptop, the benchmark on the Ryzen machine. Only
  expansion counts, depth profiles and within-run time shares are compared, which do not
  depend on the machine.
- *Line-up chosen on the test set.* The decision to drop T2 for T4 was taken on the same
  2 746 levels used to evaluate it, with no held-out set. A check on the existing data
  shows the decision does not depend on which levels it is taken on. Over 10 000 random
  splits into two halves, the selection half prefers the new line-up every time, and the
  coverage gain on the other half has median 5.0 points (95% of splits between 4.1 and
  5.8, minimum 3.4). Leaving out each of the 97 collections in turn and deciding on the
  rest also gives the new line-up every time; the new line-up gains levels in 53
  collections, ties in 43 and loses one level in one. This covers the line-up decision
  only. It says nothing about levels unlike those in the set.
- *No external reference.* Every comparison here is between versions of SokoBot. No other
  solver was run on these levels, so the report does not place the 95.6% against
  programs such as FESS, YASS or JSoko. The nearest reference point is the _Original &
  Extra_ collection, the source of the 90-level set on which Rolling Stone and FESS are
  usually reported @junghanns2001 @shoham2020. The benchmark contains 57 of its levels,
  and the new line-up solves 40 of them in 10 s (the old line-up also 40). The level files
  were not checked against the published set and the time limits differ, so this is an
  orientation, not a comparison.
- *Tuning on one level.* The goal-room thread's parameters were tuned on level 11 alone;
  @tab:sweep shows local optimality there, not elsewhere. The exchange-rate sweep
  (@tab:band) and the corral variants (@tab:corral) are also level-11 results. The only
  evidence that the thread works elsewhere is its record in the benchmark (@tab:wins).
- *Generated instances are easy.* Most are solved in under $10^4$ expansions, and their
  goal rooms are rectangles with one gap. They test correctness more than performance.
- *Historical numbers.* The figures of @sec:history are taken from commit messages, two
  archived benchmark files and audit logs written at the time. They were not re-measured
  for this report, their builds were not preserved as binaries, and the logs do not name
  the machine. They document the order of events, not effect sizes.
- *Reconstruction.* Level 11 was reconstructed for the early experiments and validated by
  its first search depths; the benchmark uses the collection's own file.
- *Soundness.* The stuck-barrier and barrier-2×2 rules are sound only relative to the
  baseline (P2) test. The optimistic corral scope, tunnel macros and the packing-order macro
  are incomplete, which @tab:dry measures. Returned solutions are always replayed, so
  correctness is unaffected; completeness of individual threads is.
- *Model correspondence.* The Lean development formalises abstractions of the code; their
  correspondence with the C++ was checked by reading, not mechanically.
- *Variance in expansion studies.* A random tie-breaker makes long plateaus seed-sensitive.
  @tab:corral, @tab:band and @tab:accounting use five seeds; earlier rows are single runs.

// =====================================================================================
= Related work
// =====================================================================================

Rolling Stone @junghanns2001 @junghanns1999 introduced most of the domain machinery used
here: tunnel and goal macros, PI-corral pruning, and deadlock tables. The goal-room macro here
is a restricted goal macro with an explicit feasibility condition and a single
precomputed order. Botea et al. @botea2002 abstract a level into rooms and tunnels and
plan at that level. FESS @shoham2020 replaces a single scalar priority with a feature
space (packing progress, connectivity, room connectivity, out-of-plan boxes) and cycles
over feature cells, which addresses exactly the plateau behaviour analysed in
@sec:failure. Algorithm portfolios @gomes2001 motivate racing diverse configurations; the
marginal-contribution measure is a simple tool for pruning such portfolios.

Preferring to keep pushing the same box is an old idea in different roles. Rolling Stone
uses it for move ordering inside IDA\* @junghanns2001. Zubaran and Ritt @zubaran2011 and
Pereira et al. @pereira2016 use inertia to break ties among states of equal $f$-value in
optimal A\* search, where it cannot change which solution length is found. The switch
penalty of @sec:switch differs in being a weighted, path-cumulative term in the primary key
of a satisficing search; no earlier work was found that prices box switches against
heuristic progress or analyses the resulting exchange rate. Backward search over pull moves
is standard in Sokoban, both for computing push distances and as a search direction; the
thread here uses it only as a portfolio member, without meeting the forward threads.

// =====================================================================================
= A proposed single-threaded algorithm <sec:proposal>
// =====================================================================================

The portfolio answers "which search suits this level?" by running four and keeping the
first to finish. The findings above suggest a single algorithm that makes the same choice
inside one thread. It is stated as a proposal: its components are each supported by a
measurement in this report, but the combination has not been implemented in the solver or
evaluated.

A _lane_ is a best-first search with its own priority queue. The algorithm runs a forward
lane $F$ (pushes from the start, priority $w_h h + w_sigma sigma + w_g g$ with
$w_sigma slash w_h = 2$, bounded recovery) and a backward lane $B$ (pulls from the solved
position, priority $w_h h$, no switch term). Each lane has its own state table, and both
tables use the same canonical key (sorted boxes, minimum cell of the player region), so a
position has the same key in either.

#block(breakable: false, inset: (left: 1em), stroke: (left: 0.5pt + luma(160)))[
  #set text(size: 9pt)
  #set par(justify: false, leading: 0.5em)
  *repeat* until time or memory runs out: \
  #h(1em) $L$ ← next lane in turn ($F$, $B$, $F$, $B$, …) \
  #h(1em) *repeat* 1 000 times: \
  #h(2em) $s$ ← best state of $L$'s queue \
  #h(2em) *for each* child $c$ of $s$ (push in $F$, pull in $B$) not pruned and not in $L$'s table: \
  #h(3em) insert $c$ into $L$'s table with parent $s$ \
  #h(3em) *if* $c$ is in the other lane's table: *return* the two half-paths joined at $c$ \
  #h(3em) *if* $c$ is the goal ($F$) or the start ($B$): *return* its path \
  #h(3em) add $c$ to $L$'s queue \
  #h(1em) *if* $F$'s queue is empty and $F$ used incomplete pruning: switch that pruning off
  and re-queue $F$'s table \
  #h(1em) *if* both queues are empty: *return* unsolvable
]

A joined path is the forward path to $c$ followed by the backward lane's pulls from $c$ to
its root, replayed in reverse as pushes (the reversal proposition of @sec:backward applies
with $c$ in place of the start). Every returned path is replayed from the start before it is
accepted.

#figure(
  table(
    columns: 2,
    align: (left, left),
    table.hline(),
    [*Design rule*], [*Evidence in this report*],
    table.hline(stroke: 0.4pt),
    [forward lane with switch penalty, ratio 2], [exchange-rate sweep (@sec:exchange); switch-penalised threads win most benchmark levels (@tab:wins)],
    [backward lane], [C-10: 114× fewer expansions; 144 of 145 gained levels (@sec:bench)],
    [no switch penalty in the backward lane], [1-13 and 19 of 120 hard levels (@tab:direction)],
    [both directions, taking turns], [C-10 favours backward, C-5 forward (@tab:showcase); 19-26 stalls from both ends (@tab:residual)],
    [bounded recovery in the forward lane], [queue maintenance 47% → 4% (@tab:residual)],
    [meeting table], [prototype only: 1-49 and 22-14 solved in under 1 s after every thread alone had failed],
    [retry with incomplete pruning off], [114 exhaustions on solvable levels (@tab:dry); 17-39 (@tab:residual); prototype on five levels],
    table.hline(),
  ),
  caption: [Rules of the proposed algorithm and the measurement behind each. The last two
    rows rest on prototypes that are not part of the evaluated solver.],
) <tab:proposal>

Alternating several queues inside one search is known from planning @roger2010, and
bidirectional search with a meeting test is classical @pohl1971. What this report adds to the design is the
configuration rule (penalise switches forward, never backward) and the evidence for each
component. Three questions are open and are the natural next experiments: whether one thread
taking turns solves as many levels per CPU-second as the four-thread portfolio; whether a
scheduler that favours the lane whose best $h$ improved recently beats strict alternation;
and how often the two searches meet at benchmark scale.

// =====================================================================================
= Conclusion and future work
// =====================================================================================

The switch penalty is the thread that runs through this work. It is a one-line change to a
priority function that encodes a human habit, finishing one box before starting the next,
and it is the reason the goal-room macro and the delivery macro pay off: both turn a box's
whole trip into one decision, which is what a high switch penalty already prefers. Its
exchange rate has a measurable optimum whose lower edge a simple sibling model predicts, and
its usefulness depends on direction: the same term that focuses a forward search stops a
backward search from unpacking the goal area.

The level-11 study showed that careful reading of telemetry can turn an apparently hard
instance into a diagnosable one: the baseline did not lack time but spent its memory on a
breadth-first sweep induced by its own recovery schedule. Traces of further levels show the
limits of that diagnosis. Bounded recovery removes the overhead on every level examined, but
some levels keep a plateau that forward search does not cross, and the backward thread
crosses it instead. Some levels are hard only from one end.

At benchmark scale the redesigned portfolio solves 95.6% of 2 746 levels in 10 s, 145 more
than before, and is faster on the levels both solve. This was measured with a backward
thread that still carried a switch penalty; without it that thread alone solves 19 of the
120 remaining levels in 5 s. The per-thread data also show that incomplete pruning, not
only search effort, costs coverage.

Future work, in order of expected value:
(i) rerun the benchmark with the corrected backward thread and a larger state limit;
(ii) implement and evaluate the algorithm of @sec:proposal, starting with the meeting table,
which is the least tested component;
(iii) make the matching heuristic incremental: a child differs from its parent by one box, so
the parent's dual solution can be reused and only one row re-matched, which should cut the
per-expansion cost on levels with many boxes;
(iv) a sound corral scope that keeps the optimistic variant's speed;
(v) multi-entrance rooms and storage-aware packing; and
(vi) FESS-style feature ranking @shoham2020 in place of the scalar priority @eq:phi.

#bibliography("refs.bib", style: "ieee", title: "References")

// =====================================================================================
#pagebreak()
#heading(numbering: none)[Appendix A: Thread parameters]

#figure(
  table(
    columns: 6,
    align: (left, right, right, right, right, right),
    table.hline(),
    [*Parameter*], [*T0*], [*T1 (goal room)*], [*T2*], [*T3*], [*T4 (backward)*],
    table.hline(stroke: 0.4pt),
    [direction], [push], [push], [push], [push], [pull],
    [$w_h$], [$10^6$], [$10^6$], [$10^6$], [$10^6$], [$10^6$],
    [$w_sigma$], [$2 dot 10^6$], [$2 dot 10^6$], [$2.5 dot 10^6$], [0], [0#super[†]],
    [$w_g$], [$5 dot 10^4$], [$5 dot 10^4$], [0], [0], [0],
    [recovery], [full re-key], [bounded], [none], [pocket sink], [none],
    [matching], [Hungarian], [Hungarian], [Hungarian], [greedy + exact], [Hungarian (to starts)],
    [room / delivery macros], [yes / yes], [yes / yes], [yes / yes], [yes / yes], [no / no],
    [corral rules], [PI], [PI + refinements, optimistic], [PI], [PI], [none],
    [state limit], [$10^7$], [$10^7$], [$10^7$], [$10^7$], [$4 dot 10^6$],
    [default line-up], [yes], [yes], [no], [yes], [yes],
    table.hline(),
  ),
  caption: [Thread configurations (`native/include/flags.h`). Bounded recovery uses
    $Delta = 1.25 dot 10^5$ and $rho_max = 10^6$. #super[†]The benchmark of @sec:bench ran
    T4 with $w_sigma = 7.5 dot 10^5$; 0 is the corrected value (@sec:direction).],
)

#heading(numbering: none)[Appendix B: Lean 4 development]

Parts of this report were formalised in Lean 4 (all proofs checked, no `sorry` or added
axioms). The Lean sources are kept separately and are not reproduced here, so a reader
cannot check them from this document; the table below is an index, not evidence.

What the formalisation was worth, measured by outcome: it produced the first-exit argument
behind the stuck-barrier and barrier-2×2 rules, and it located the cause of the two false
deadlocks of @tab:corral (a corral without goals). It did not predict which ideas help.
Two formalised ideas, sink forgiveness and switch-dominated replacement, are proved to
improve individual keys and fail in measurement (@tab:accounting), and the upper edge of
the corridor band is proved in the model and refuted by the sweep (@tab:band). A proof
here is a statement about the model.

Theorems, by the names reported, and where they appear:

#table(
  columns: 2,
  align: (left, left),
  table.hline(),
  [*Theorem*], [*Statement in this report*],
  table.hline(stroke: 0.4pt),
  [`prop7_breadth_first_degeneration`], [breadth-first degeneration (@sec:failure)],
  [`level11_rebuild_760_collapses_to_bfs`], [$lambda_760 approx 115$ estimate],
  [`eq7_bounded_recovery_invariant`], [$lambda <= 1.05$ (@sec:recovery)],
  [`prop12_level11_interior_collapse`], [interior abstraction, 519 252 → 15],
  [`prop17_greedy_fails_where_exact_succeeds`], [greedy matching failure (@sec:matching)],
  [`stuck_barrier_sound_at_first_barrier_push`], [first-barrier-push lemma, stuck barrier],
  [`golden_switch_band`], [open-board band (@sec:exchange)],
  [`table5_corridor_detour_threshold`], [corridor-detour band; upper edge refuted by @tab:band],
  [`corridor_band_with_depth_cost`], [depth-adjusted edge $r < 3 - lambda$; mixed results in @tab:edge],
  [`sink_goal_forgiveness_saves_tax`, `sink_goal_prevents_doorway_clogging`], [sink forgiveness; not beneficial in @tab:accounting],
  [`tt_dominance_strictly_improves_priority`, `_irreflexive`, `_transitive`], [replacement order; harmful in @tab:accounting],
  table.hline(),
)

#heading(numbering: none)[Appendix C: Reproduction]

```
# single-thread trace with overrides, no rebuild needed
bun run solve 1-11_Sokoban-Original-and-Extra-11 --thread 1 --set corral_optimistic=0
# untraced timing of one thread
SOKO_THREADS=1 native/bin/sokosolve < maps/1-11_Sokoban-Original-and-Extra-11.txt
# full benchmark, one line-up (writes a CSV with per-thread outcomes)
SOKO_THREADS=0,1,3,4 native/bin/sokobench list.txt maps 10 analysis/data/new.csv
# statistics of the full-benchmark section
cd analysis && uv run jupyter lab thesis_results.ipynb
# figures for this report
python3 make_figures.py && typst compile main.typ
```
