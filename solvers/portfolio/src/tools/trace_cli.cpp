#ifndef SOKO_TRACE
#error "trace_cli.cpp must be built with -DSOKO_TRACE"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "flags.h"
#include "soko/map_loader.h"
#include "sokoban.h"

namespace {

constexpr size_t kMaxNodes = 10'000'000;
constexpr size_t kDetailBudget = 600'000;
constexpr u32 kEventsPerClockCheck = 255;
constexpr u32 kRootMove = 0xFFFFFFFFu;
constexpr int kReasonCount = 9;
constexpr int kRejectedReasonSlots = 8;
constexpr int kPhaseCount = 20;
constexpr int kFirstSubstepPhase = 11;
constexpr int kChildPhase = 9;
constexpr int kSolutionPhase = 10;

enum Fate : u8 { kOpen = 0, kExpanded = 1, kSolution = 2, kDropped = 3 };

struct Tree {
  std::vector<int32_t> parent;
  std::vector<u32> move, push_count, heuristic, expansion_order, step_microseconds;
  std::vector<u8> fate, drop_reason, rejected;
  std::vector<int32_t> detail_parent;
  std::vector<u32> detail_move;
  std::vector<u8> detail_reason;
  std::vector<u32> index_plus_one_of_id;
  u64 reason_totals[kReasonCount] = {};
};

Tree g_tree;
u32 g_pops = 0;
u32 g_max_pops = 0;
double g_max_seconds = 0.0;
const char* g_stop_reason = "";
volatile std::sig_atomic_t g_stop_requested = 0;
bool g_solved = false;
u32 g_events = 0;
std::chrono::steady_clock::time_point g_started;
std::chrono::steady_clock::time_point g_previous_event;
std::chrono::steady_clock::time_point g_current_pop_started;
bool g_has_previous_event = false;
int32_t g_current_pop_index = -1;
u64 g_phase_nanoseconds[kPhaseCount] = {};
u64 g_total_nanoseconds = 0;

using Clock = std::chrono::steady_clock;

u64 NanosecondsBetween(Clock::time_point from, Clock::time_point to) {
  return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count());
}

int PhaseOf(const TraceEvent& event) {
  switch (event.kind) {
    case TraceKind::kPop: return 0;
    case TraceKind::kPruned:
    case TraceKind::kNodePruned: return static_cast<int>(event.reason);
    case TraceKind::kChild: return kChildPhase;
    case TraceKind::kSolution: return kSolutionPhase;
    case TraceKind::kPhase: return kFirstSubstepPhase + static_cast<int>(event.g);
  }
  return 0;
}

void StopSearch(const char* reason) {
  g_stop_reason = reason;
  g_solution_found.store(true);
}

void RequestStop(int) { g_stop_requested = 1; }

// `--set key=value` on the traced thread's config, for parameter sweeps without a rebuild.
bool ApplySetting(ThreadConfig* cfg, const char* setting) {
  const char* eq = std::strchr(setting, '=');
  if (!eq) return false;
  const std::string key(setting, eq - setting);
  const char* value = eq + 1;
  struct Field { const char* name; u64 ThreadConfig::*member; };
  static constexpr Field kFields[] = {
      {"h_weight", &ThreadConfig::h_weight},
      {"switch_penalty", &ThreadConfig::switch_penalty},
      {"g_weight", &ThreadConfig::g_weight},
      {"walk_weight", &ThreadConfig::walk_weight},
      {"noise_weight", &ThreadConfig::noise_weight},
      {"switch_penalty_floor", &ThreadConfig::switch_penalty_floor},
      {"max_g_rate", &ThreadConfig::max_g_rate},
      {"g_rate_step", &ThreadConfig::g_rate_step},
  };
  for (const Field& field : kFields) {
    if (key == field.name) {
      cfg->*field.member = std::strtoull(value, nullptr, 10);
      return true;
    }
  }
  struct Flag { const char* name; bool ThreadConfig::*member; };
  static constexpr Flag kFlags[] = {
      {"goal_room_macro", &ThreadConfig::goal_room_macro},
      {"room_heuristic", &ThreadConfig::room_heuristic},
      {"goal_room_funnel", &ThreadConfig::goal_room_funnel},
      {"aggressive_corral", &ThreadConfig::aggressive_corral},
      {"linear_conflict", &ThreadConfig::linear_conflict},
      {"delivery_macro", &ThreadConfig::delivery_macro},
      {"corral_stuck_boxes", &ThreadConfig::corral_stuck_boxes},
      {"corral_optimistic", &ThreadConfig::corral_optimistic},
      {"greedy_exact_fallback", &ThreadConfig::greedy_exact_fallback},
  };
  for (const Flag& flag : kFlags) {
    if (key == flag.name) {
      cfg->*flag.member = std::atoi(value) != 0;
      return true;
    }
  }
  if (key == "delivery_state_limit") {
    cfg->delivery_state_limit = static_cast<u32>(std::strtoul(value, nullptr, 10));
    return true;
  }
  if (key == "delivery_candidates") {
    cfg->delivery_candidates = static_cast<u32>(std::strtoul(value, nullptr, 10));
    return true;
  }
  if (key == "stagnation") {
    const std::string mode(value);
    if (mode == "none") cfg->stagnation_mode = StagnationMode::kNone;
    else if (mode == "full") cfg->stagnation_mode = StagnationMode::kFullReheap;
    else if (mode == "pocket") cfg->stagnation_mode = StagnationMode::kPocketSink;
    else if (mode == "bounded") cfg->stagnation_mode = StagnationMode::kBoundedReheap;
    else return false;
    return true;
  }
  return false;
}

u32 PackMove(const TraceEvent& event) {
  return (static_cast<u32>(event.from_row) << 24) | (static_cast<u32>(event.from_column) << 16) |
         (static_cast<u32>(event.to_row) << 8) | static_cast<u32>(event.to_column);
}

int32_t NodeIndexOf(u32 id) {
  if (id >= g_tree.index_plus_one_of_id.size()) return -1;
  return static_cast<int32_t>(g_tree.index_plus_one_of_id[id]) - 1;
}

void AddNode(u32 id, int32_t parent, u32 move, u32 g, u32 h, u8 fate) {
  Tree& tree = g_tree;
  if (id >= tree.index_plus_one_of_id.size()) {
    tree.index_plus_one_of_id.resize(std::max<size_t>(id + 1, tree.index_plus_one_of_id.size() * 2), 0);
  }
  tree.index_plus_one_of_id[id] = static_cast<u32>(tree.parent.size()) + 1;
  tree.parent.push_back(parent);
  tree.move.push_back(move);
  tree.push_count.push_back(g);
  tree.heuristic.push_back(h);
  tree.expansion_order.push_back(0);
  tree.step_microseconds.push_back(0);
  tree.fate.push_back(fate);
  tree.drop_reason.push_back(0);
  tree.rejected.resize(tree.rejected.size() + kRejectedReasonSlots, 0);
}

void RecordRejectedPush(const TraceEvent& event) {
  Tree& tree = g_tree;
  const int reason = static_cast<int>(event.reason);
  ++tree.reason_totals[reason];

  const int32_t parent = NodeIndexOf(event.parent);
  if (parent < 0) return;

  u8& count = tree.rejected[static_cast<size_t>(parent) * kRejectedReasonSlots + (reason - 1)];
  if (count < 255) ++count;
  if (tree.detail_parent.size() >= kDetailBudget) return;

  tree.detail_parent.push_back(parent);
  tree.detail_move.push_back(PackMove(event));
  tree.detail_reason.push_back(static_cast<u8>(reason));
}

void CloseCurrentPop(Clock::time_point now) {
  if (g_current_pop_index < 0) return;

  const u64 microseconds = NanosecondsBetween(g_current_pop_started, now) / 1000;
  g_tree.step_microseconds[g_current_pop_index] =
      static_cast<u32>(std::min<u64>(microseconds, UINT32_MAX));
}

void OnEvent(const TraceEvent& event) {
  if (*g_stop_reason != '\0' || g_solved) return;
  if (g_stop_requested) return StopSearch("stopped");

  const Clock::time_point now = Clock::now();
  if (g_has_previous_event) {
    g_phase_nanoseconds[PhaseOf(event)] += NanosecondsBetween(g_previous_event, now);
  }
  g_previous_event = now;
  g_has_previous_event = true;

  Tree& tree = g_tree;
  switch (event.kind) {
    case TraceKind::kPop: {
      if (tree.parent.empty()) AddNode(event.node, -1, kRootMove, 0, 0, kOpen);

      const int32_t index = NodeIndexOf(event.node);
      CloseCurrentPop(now);
      g_current_pop_index = index;
      g_current_pop_started = now;
      if (index >= 0) {
        tree.expansion_order[index] = ++g_pops;
        tree.fate[index] = kExpanded;
      }
      if (g_max_pops > 0 && g_pops >= g_max_pops) StopSearch("expansions");
      break;
    }
    case TraceKind::kChild:
    case TraceKind::kSolution: {
      const bool is_solution = event.kind == TraceKind::kSolution;
      AddNode(event.node, NodeIndexOf(event.parent), PackMove(event), event.g, event.h,
              is_solution ? kSolution : kOpen);
      g_solved = is_solution;
      break;
    }
    case TraceKind::kPhase:
      break;
    case TraceKind::kPruned:
      RecordRejectedPush(event);
      break;
    case TraceKind::kNodePruned: {
      const int32_t index = NodeIndexOf(event.node);
      if (index < 0) break;

      tree.fate[index] = kDropped;
      tree.drop_reason[index] = static_cast<u8>(event.reason);
      ++tree.reason_totals[static_cast<int>(event.reason)];
      break;
    }
  }

  if (tree.parent.size() >= kMaxNodes) StopSearch("size");
  if (g_max_seconds <= 0 || (++g_events & kEventsPerClockCheck) != 0) return;

  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - g_started).count();
  if (seconds >= g_max_seconds) StopSearch("time");
}

void WriteBytes(const void* data, size_t bytes) {
  static const char kPadding[4] = {0, 0, 0, 0};
  std::fwrite(data, 1, bytes, stdout);
  std::fwrite(kPadding, 1, (4 - bytes % 4) % 4, stdout);
}

template <typename T>
void WriteArray(const std::vector<T>& values) {
  WriteBytes(values.data(), values.size() * sizeof(T));
}

void WriteTree(int thread_id, const soko::MapGrids& grids) {
  const Tree& tree = g_tree;
  char header[2048];
  int length = std::snprintf(
      header, sizeof(header),
      "{\"threadId\":%d,\"backward\":%s,\"width\":%d,\"height\":%d,\"expansionCount\":%u,\"stopReason\":\"%s\","
      "\"isSolved\":%s,\"nodeCount\":%zu,\"detailCount\":%zu,\"reasonTotals\":[",
      thread_id, g_trace_config.backward ? "true" : "false", grids.width, grids.height, g_pops, g_stop_reason, g_solved ? "true" : "false",
      tree.parent.size(), tree.detail_parent.size());
  for (int i = 0; i < kReasonCount; ++i) {
    length += std::snprintf(header + length, sizeof(header) - length, "%s%llu", i == 0 ? "" : ",",
                            static_cast<unsigned long long>(tree.reason_totals[i]));
  }
  length += std::snprintf(header + length, sizeof(header) - length,
                          "],\"totalNanoseconds\":%llu,\"phaseNanoseconds\":[",
                          static_cast<unsigned long long>(g_total_nanoseconds));
  for (int i = 0; i < kPhaseCount; ++i) {
    length += std::snprintf(header + length, sizeof(header) - length, "%s%llu", i == 0 ? "" : ",",
                            static_cast<unsigned long long>(g_phase_nanoseconds[i]));
  }
  length += std::snprintf(header + length, sizeof(header) - length, "],\"switchPenalty\":%llu}",
                          static_cast<unsigned long long>(g_trace_config.switch_penalty));

  const uint32_t header_length = static_cast<uint32_t>(length);
  std::fwrite(&header_length, 4, 1, stdout);
  WriteBytes(header, header_length);
  WriteArray(tree.parent);
  WriteArray(tree.move);
  WriteArray(tree.push_count);
  WriteArray(tree.heuristic);
  WriteArray(tree.expansion_order);
  WriteArray(tree.step_microseconds);
  WriteArray(tree.fate);
  WriteArray(tree.drop_reason);
  WriteArray(tree.rejected);
  WriteArray(tree.detail_parent);
  WriteArray(tree.detail_move);
  WriteArray(tree.detail_reason);
}

}

int main(int argc, char** argv) {
  int thread_id = 0;
  std::vector<const char*> settings;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (std::strcmp(argv[i], "--thread") == 0) thread_id = std::atoi(argv[i + 1]);
    if (std::strcmp(argv[i], "--set") == 0) settings.push_back(argv[i + 1]);
    if (std::strcmp(argv[i], "--pops") == 0) g_max_pops = static_cast<u32>(std::atoi(argv[i + 1]));
    if (std::strcmp(argv[i], "--seconds") == 0) g_max_seconds = std::atof(argv[i + 1]);
  }
  if (thread_id < 0 || thread_id >= kThreadCount) {
    std::fprintf(stderr, "thread must be 0..%d\n", kThreadCount - 1);
    return 2;
  }

  g_trace_config = kThreadConfigs[thread_id];
  for (const char* setting : settings) {
    if (!ApplySetting(&g_trace_config, setting)) {
      std::fprintf(stderr, "bad --set %s\n", setting);
      return 2;
    }
  }
  g_trace_config_override = !settings.empty();

  soko::MapGrids grids;
  if (!soko::ParseMapText(std::cin, &grids)) {
    std::fprintf(stderr, "empty map\n");
    return 2;
  }

  soko::RowPointers rows = soko::BuildRowPointers(&grids);

  std::signal(SIGTERM, RequestStop);
  std::signal(SIGINT, RequestStop);
  g_trace_thread_id = thread_id;
  g_trace_event = OnEvent;
  g_started = Clock::now();
  std::free(SolveSokoban(grids.width, grids.height, rows.map.data(), rows.items.data()));
  const Clock::time_point finished = Clock::now();
  g_total_nanoseconds = NanosecondsBetween(g_started, finished);
  // The solver ran out of state pool: a capacity stop, not an exhausted search space.
  if (!g_solved && *g_stop_reason == '\0' && g_pool_exhausted.load()) g_stop_reason = "pool";
  CloseCurrentPop(finished);

  WriteTree(thread_id, grids);
  return 0;
}
