"""Builds the manuscript's SVG figures from the telemetry CSVs in data/."""
import csv
import os

import matplotlib

matplotlib.use("svg")
import matplotlib.pyplot as plt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
BENCH = os.path.join(HERE, "..", "analysis", "data")  # full-benchmark runs live with the analysis
OUT = os.path.join(HERE, "figures")

BLUE, ORANGE, AQUA, YELLOW, MAGENTA = "#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"
INK, INK2, GRID = "#1f1f1e", "#5f5e58", "#e4e3de"

plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["DejaVu Serif"],
    "font.size": 8,
    "axes.edgecolor": INK2,
    "axes.labelcolor": INK,
    "axes.linewidth": 0.6,
    "xtick.color": INK2,
    "ytick.color": INK2,
    "xtick.major.width": 0.6,
    "ytick.major.width": 0.6,
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.5,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "svg.fonttype": "path",
    "figure.dpi": 150,
})


def read(name):
    with open(os.path.join(DATA, name)) as f:
        return list(csv.DictReader(f))


def save(fig, name):
    fig.savefig(os.path.join(OUT, name), bbox_inches="tight", pad_inches=0.03)
    plt.close(fig)


# --- Figure: baseline cost per expansion and best h (two panels, one shared x) ------
rows = read("baseline-progress-raw.csv")
xs, us, bh = [], [], []
step = 20
for i in range(step, len(rows), step):
    a, b = rows[i - step], rows[i]
    de = int(b["expansion"]) - int(a["expansion"])
    dt = int(b["cumulative_microseconds"]) - int(a["cumulative_microseconds"])
    xs.append(int(b["expansion"]) / 1e6)
    us.append(dt / de)
    bh.append(int(b["best_heuristic"]))

fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(3.4, 3.0), sharex=True,
                               gridspec_kw={"hspace": 0.18})
ax1.plot(xs, us, color=BLUE, lw=1.6)
ax1.set_ylabel("µs per expansion")
ax1.set_ylim(0, 17)
ax2.step(xs, bh, where="post", color=ORANGE, lw=1.6)
ax2.set_ylabel("best $h$ so far")
ax2.set_xlabel("expansions (millions)")
ax2.axvline(2.86, color=INK2, lw=0.6, ls=(0, (3, 2)))
ax2.annotate("last improvement\n(h = 156)", xy=(2.86, 156), xytext=(3.6, 182),
             fontsize=7, color=INK2, arrowprops={"arrowstyle": "-", "color": INK2, "lw": 0.5})
for ax in (ax1, ax2):
    ax.tick_params(labelsize=7)
save(fig, "baseline-telemetry.svg")


# --- Figure: best-h progress for the ablation sequence (log x) ----------------------
def progress_raw(name):
    r = read(name)
    return [int(x["expansion"]) for x in r], [int(x["best_heuristic"]) for x in r]


def progress(name):
    r = read(name)
    return [max(1, int(x["expansion"])) for x in r], [int(x["best_heuristic"]) for x in r]


series = [
    ("baseline T0 (fails)", *progress_raw("baseline-progress-raw.csv"), BLUE, "-", True),
    ("+ bounded recovery (fails)", *progress_raw("bounded-progress-raw.csv"), ORANGE, (0, (4, 2)), True),
    ("+ goal-room macro", *progress_raw("roommacro-progress-raw.csv"), AQUA, "-", False),
    ("+ fixed switch penalty", *progress("fixedsp-progress.csv"), YELLOW, (0, (1, 1.5)), False),
    ("final goal-room thread", *progress("final2-progress.csv"), MAGENTA, "-", False),
]
fig, ax = plt.subplots(figsize=(6.6, 2.9))
for label, x, y, color, ls, failed in series:
    ax.step(x, y, where="post", color=color, lw=1.6, ls=ls, label=label)
    end_x, end_y = x[-1], y[-1]
    ax.plot([end_x], [end_y], marker="x" if failed else "o", ms=5, color=color,
            mec=color, mew=1.4, ls="none")
ax.set_xscale("log")
ax.set_xlim(1e4, 8e6)
ax.set_ylim(0, 260)
ax.set_xlabel("expansions (log scale)")
ax.set_ylabel("best $h$ so far")
ax.legend(loc="lower left", fontsize=7, frameon=False, handlelength=2.6)
ax.text(5.9e6, 166, "pool full", fontsize=7, color=INK2, ha="center")
ax.text(4.0e6, 18, "solved", fontsize=7, color=INK2, ha="center")
ax.tick_params(labelsize=7)
save(fig, "progress-ablation.svg")


# --- Figure: depth profile, baseline vs final -----------------------------------------
base = read("baseline-depth-raw.csv")
fin = read("final2-depth.csv")
fig, ax = plt.subplots(figsize=(3.4, 2.3))
ax.plot([int(r["depth"]) for r in base], [int(r["states"]) / 1e3 for r in base],
        color=BLUE, lw=1.6)
ax.plot([int(r["depth"]) for r in fin], [int(r["states"]) / 1e3 for r in fin],
        color=MAGENTA, lw=1.6, ls=(0, (4, 2)))
ax.annotate("baseline T0\n(10.0M states, unsolved)", xy=(45, 230), xytext=(70, 200), fontsize=7, color=INK,
            arrowprops={"arrowstyle": "-", "color": INK2, "lw": 0.5})
ax.annotate("final goal-room thread\n(0.21M states, solved)", xy=(24, 26), xytext=(70, 90), fontsize=7, color=INK,
            arrowprops={"arrowstyle": "-", "color": INK2, "lw": 0.5})
ax.set_xlabel("depth (pushes or macro moves from the root)")
ax.set_ylabel("states generated (thousands)")
ax.tick_params(labelsize=7)
save(fig, "depth-profile.svg")


# --- Figure: 1-11 expansions per configuration (horizontal bars, log x) ---------------
configs = [
    ("Bounded recovery, decaying penalty", 6_205_940, True),
    ("Baseline T0", 5_896_802, True),
    ("+ goal-room macro", 4_513_652, False),
    ("+ fixed switch penalty (2M)", 1_569_753, False),
    ("+ delivery macro", 1_088_619, False),
    ("+ stuck-barrier rule", 986_825, False),
    ("+ dead-target rule, sound", 532_107, False),
    ("+ dead-target rule, optimistic", 227_331, False),
    ("+ barrier 2x2, room-switch forgiveness", 125_990, False),
]
fig, ax = plt.subplots(figsize=(6.6, 2.75))
labels = [c[0] for c in configs][::-1]
vals = [c[1] for c in configs][::-1]
fails = [c[2] for c in configs][::-1]
bars = ax.barh(range(len(vals)), vals, height=0.62,
               color=[GRID if f else BLUE for f in fails],
               edgecolor=[INK2 if f else BLUE for f in fails], linewidth=0.6)
for i, (v, f) in enumerate(zip(vals, fails)):
    ax.text(v * 1.08, i, f"{v / 1e6:.2f}M" + ("  unsolved (pool full)" if f else ""),
            va="center", fontsize=7, color=INK)
ax.set_yticks(range(len(vals)), labels, fontsize=7)
ax.set_xscale("log")
ax.set_xlim(1e5, 4e7)
ax.set_xlabel("expansions on Original-11 (log scale)")
ax.grid(axis="y", visible=False)
ax.tick_params(labelsize=7)
save(fig, "ablation-bars.svg")

print("figures written to", OUT)


# --- Figure: switch-penalty sweep on level 11 (5 seeds each) --------------------------
rows = read("switch-sweep.csv")
by = {}
for r in rows:
    by.setdefault(float(r["ratio"]), []).append(int(r["expansions"]) / 1e3)
ratios = sorted(by)
fig, ax = plt.subplots(figsize=(3.4, 2.3))
ax.axvspan(2.0, 3.0, color="#cde2fb", lw=0, zorder=0)
ax.text(2.5, 640, "predicted band\n$2 \\leq w_\\sigma/w_h < 3$", fontsize=7, color=INK2, ha="center", va="top")
for x in ratios:
    ax.plot([x] * len(by[x]), by[x], "o", ms=3.2, color=BLUE, alpha=0.75, mec="none", zorder=3)
ax.plot(ratios, [sum(v) / len(v) for v in (by[x] for x in ratios)], color=BLUE, lw=1.4, zorder=2)
ax.set_xlabel("switch penalty $w_\\sigma / w_h$")
ax.set_ylabel("expansions (thousands)")
ax.set_ylim(0, 700)
ax.set_xticks([1.75, 2.0, 2.25, 2.5, 2.75, 3.0, 3.25])
ax.tick_params(labelsize=7)
save(fig, "switch-sweep.svg")


# --- Full benchmark (2 746 levels, 10 s limit, 16-thread Ryzen) ------------------------
def bench(name):
    with open(os.path.join(BENCH, name)) as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        r["ok"] = r["status"] == "solved" and r["valid"] == "1"
        r["time_ms"] = float(r["time_ms"])
        r["boxes"] = int(r["boxes"])
    return rows


old_run, new_run = bench("old.csv"), bench("new.csv")
GREY = "#8a8984"

# Cactus plot: maps solved within a time budget.
fig, ax = plt.subplots(figsize=(4.6, 2.5))
for rows, colour, label in ((old_run, GREY, "old line-up (T0 T1 T2 T3)"),
                            (new_run, BLUE, "new line-up (T0 T1 T3 T4)")):
    t = sorted(max(r["time_ms"], 1.0) for r in rows if r["ok"])
    ax.step(t, range(1, len(t) + 1), where="post", color=colour, lw=1.3, label=label)
ax.axhline(len(new_run), color=INK2, lw=0.6, ls=":")
ax.text(1.2, len(new_run) - 15, "2 746 levels", fontsize=6.5, color=INK2, va="top")
ax.set_xscale("log")
ax.set_xlim(1, 10_000)
ax.set_ylim(1700, 2800)
ax.set_xlabel("time budget per level (ms, log scale)")
ax.set_ylabel("levels solved")
ax.legend(frameon=False, fontsize=7, loc="lower right")
ax.tick_params(labelsize=7)
save(fig, "cactus.svg")

# Coverage by box count with 95% Wilson intervals.
def wilson(k, n, z=1.96):
    p = k / n
    c = (p + z * z / (2 * n)) / (1 + z * z / n)
    h = z / (1 + z * z / n) * (p * (1 - p) / n + z * z / (4 * n * n)) ** 0.5
    return c - h, c + h


bins = [(1, 4), (5, 6), (7, 8), (9, 10), (11, 14), (15, 20)]
fig, ax = plt.subplots(figsize=(4.0, 2.3))
for off, (rows, colour, label) in zip((-0.12, 0.12), ((old_run, GREY, "old"), (new_run, BLUE, "new"))):
    for i, (lo, hi) in enumerate(bins):
        sel = [r for r in rows if lo <= r["boxes"] <= hi]
        k, n = sum(r["ok"] for r in sel), len(sel)
        a, b = wilson(k, n)
        ax.errorbar(i + off, 100 * k / n, yerr=[[100 * (k / n - a)], [100 * (b - k / n)]], fmt="o",
                    ms=3.2, capsize=2, lw=0.9, color=colour, label=label if i == 0 else None)
ax.set_xticks(range(len(bins)), [f"{a}–{b}" for a, b in bins])
ax.set_xlabel("boxes")
ax.set_ylabel("levels solved (%)")
ax.set_ylim(50, 101)
ax.legend(frameon=False, fontsize=7, loc="lower left")
ax.tick_params(labelsize=7)
save(fig, "coverage-boxes.svg")
