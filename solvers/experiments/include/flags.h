#ifndef NATIVE_INCLUDE_FLAGS_H_
#define NATIVE_INCLUDE_FLAGS_H_

#include "sokoban.h"

enum class Heuristic : u8 {
  kReverseBfs,
  kManhattan,
  kNone,
};

enum class StagnationMode : u8 {
  kNone,
  kFullReheap,
  kPocketSink,
  // Rebuilds the queue from stored (h, switches, g) with an O(n) heapify, grows the
  // g weight only up to max_g_rate (never past plain A*), and decays the switch
  // penalty toward switch_penalty_floor while the search stays stuck.
  kBoundedReheap,
};

struct ThreadConfig {
  bool enabled;

  bool dead_squares;
  bool two_by_two;
  bool freeze_check;
  bool pi_corral;
  bool hall_check;

  bool macro_moves;
  bool goal_room_funnel;
  bool goal_freeze_check;
  bool aggressive_corral;
  StagnationMode stagnation_mode;

  Heuristic heuristic;
  bool hungarian;
  bool linear_conflict;

  u64 h_weight;
  u64 switch_penalty;
  u64 g_weight;
  u64 walk_weight;
  u64 noise_weight;

  // kBoundedReheap only (zero for every other mode).
  u64 switch_penalty_floor = 0;
  u64 max_g_rate = 0;
  u64 g_rate_step = 0;

  // Detect a goal room with a single entrance, precompute a packing order, and treat
  // "push a box through the entrance" as one move that lands it on the next goal.
  bool goal_room_macro = false;
  // With a goal room holding every goal: h = push distance of each outside box to the
  // room entry + the precomputed in-room pushes still to come (no goal matching).
  bool room_heuristic = false;
  // With goal_room_macro: also offer "push this box all the way to the room entry" as one
  // move whenever such a path exists with the other boxes left where they are.
  bool delivery_macro = false;
  u32 delivery_state_limit = 0;  // box/player states searched per delivery attempt
  u32 delivery_candidates = 0;   // only the N outside boxes nearest the room try (0 = all)
  // PI-corral: a box outside the barrier whose every push needs a barrier box to move first
  // (or runs into a wall or dead square) cannot open the corral, so it no longer spoils PI.
  bool corral_stuck_boxes = false;
  bool corral_optimistic = false;  // see deadlock.cpp; makes the thread incomplete
  // Non-Hungarian threads: retry an exact matching before calling a state hopeless.
  bool greedy_exact_fallback = false;
};

constexpr int kThreadCount = 4;
constexpr bool kDebugPrintWinner = true;

constexpr ThreadConfig kThreadConfigs[kThreadCount] = {
    // Thread 0: Engine B: Funnel sprinter (1-5, 1-9), plus goal-room and delivery macros
    {
        .enabled = true,
        .dead_squares = true,
        .two_by_two = true,
        .freeze_check = true,
        .pi_corral = true,
        .hall_check = true,
        .macro_moves = true,
        .goal_room_funnel = true,
        .goal_freeze_check = true,
        .aggressive_corral = true,
        .stagnation_mode = StagnationMode::kFullReheap,
        .heuristic = Heuristic::kReverseBfs,
        .hungarian = true,
        .linear_conflict = true,
        .h_weight = 1'000'000,
        .switch_penalty = 2'000'000,
        .g_weight = 50'000,
        .walk_weight = 0,
        .noise_weight = 0,
        .goal_room_macro = true,
        .delivery_macro = true,
        .delivery_state_limit = 512,
        .delivery_candidates = 1,
    },
    // Thread 1: Engine E: Goal-room packer (solves 1-11). T0's pruning plus:
    //  - goal-room macro: a box entering the room jumps to the next goal of a precomputed
    //    packing order; boxes in the room never move again
    //  - delivery macro: the nearest box's whole trip to the room entry is one move
    //  - sharper PI-corral deadlocks (stuck boxes, pushes onto dead squares ignored);
    //    corral_optimistic makes this thread incomplete, which the portfolio absorbs
    //  - bounded stagnation backtracking with a fixed switch penalty
    // Replaced the old Engine C ("traffic juggler"), which overlapped T0 on almost every map.
    {
        .enabled = true,
        .dead_squares = true,
        .two_by_two = true,
        .freeze_check = true,
        .pi_corral = true,
        .hall_check = true,
        .macro_moves = true,
        .goal_room_funnel = true,
        .goal_freeze_check = true,
        .aggressive_corral = true,
        .stagnation_mode = StagnationMode::kBoundedReheap,
        .heuristic = Heuristic::kReverseBfs,
        .hungarian = true,
        .linear_conflict = false,
        .h_weight = 1'000'000,
        .switch_penalty = 2'000'000,
        .g_weight = 50'000,
        .walk_weight = 0,
        .noise_weight = 0,
        .switch_penalty_floor = 2'000'000,
        .max_g_rate = 1'000'000,
        .g_rate_step = 125'000,
        .goal_room_macro = true,
        .delivery_macro = true,
        .delivery_state_limit = 512,
        .delivery_candidates = 1,
        .corral_stuck_boxes = true,
        .corral_optimistic = true,
    },
    // Thread 2: Engine A: Classic baseline (1-7, original2), plus goal-room and delivery macros
    {
        .enabled = true,
        .dead_squares = true,
        .two_by_two = true,
        .freeze_check = true,
        .pi_corral = true,
        .hall_check = true,
        .macro_moves = true,
        .goal_room_funnel = false,
        .goal_freeze_check = false,
        .aggressive_corral = false,
        .stagnation_mode = StagnationMode::kNone,
        .heuristic = Heuristic::kReverseBfs,
        .hungarian = true,
        .linear_conflict = true,
        .h_weight = 1'000'000,
        .switch_penalty = 2'500'000,
        .g_weight = 0,
        .walk_weight = 0,
        .noise_weight = 0,
        .goal_room_macro = true,
        .delivery_macro = true,
        .delivery_state_limit = 512,
        .delivery_candidates = 1,
    },
    // Thread 3: Engine D: Zero-switch pocket-sink diver (C-10, 1-40), plus macros and exact-matching fallback
    {
        .enabled = true,
        .dead_squares = true,
        .two_by_two = true,
        .freeze_check = true,
        .pi_corral = true,
        .hall_check = true,
        .macro_moves = true,
        .goal_room_funnel = false,
        .goal_freeze_check = false,
        .aggressive_corral = false,
        .stagnation_mode = StagnationMode::kPocketSink,
        .heuristic = Heuristic::kReverseBfs,
        .hungarian = false,
        .linear_conflict = true,
        .h_weight = 1'000'000,
        .switch_penalty = 0,
        .g_weight = 0,
        .walk_weight = 0,
        .noise_weight = 0,
        .goal_room_macro = true,
        .delivery_macro = true,
        .delivery_state_limit = 512,
        .delivery_candidates = 1,
        .greedy_exact_fallback = true,
    },
};

#ifdef SOKO_TRACE
// The tracer can override the traced thread's config (`--set key=value`) for sweeps.
extern ThreadConfig g_trace_config;
extern bool g_trace_config_override;
#endif

#endif  // NATIVE_INCLUDE_FLAGS_H_
