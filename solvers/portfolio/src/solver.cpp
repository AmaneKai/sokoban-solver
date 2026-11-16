#include "soko/solver.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "flags.h"
#include "soko/backward.h"
#include "sokoban.h"

#ifdef __APPLE__
#include <pthread/qos.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

std::atomic<bool> g_solution_found{false};
std::atomic<u64> g_nodes_explored{0};
std::atomic<int> g_winning_thread{-1};
std::atomic<bool> g_pool_exhausted{false};
static_assert(kThreadCount <= kThreadSlots, "raise kThreadSlots in sokoban.h");
std::atomic<u64> g_thread_nodes[kThreadSlots];
std::atomic<u8> g_thread_outcome[kThreadSlots];

#ifdef SOKO_TRACE
int g_trace_thread_id = -1;
ThreadConfig g_trace_config{};
bool g_trace_config_override = false;
void (*g_trace_event)(const TraceEvent& event) = nullptr;

#define SOKO_TRACE_EVENT(...) EmitTrace(__VA_ARGS__)
#define SOKO_TRACE_PHASE(phase) \
  SOKO_TRACE_EVENT(TraceKind::kPhase, TraceReason::kNone, 0, 0, 0, 0, static_cast<u32>(phase), 0)
#define SOKO_PRUNE_IF(condition, why)                                        \
  if (condition) {                                                           \
    EmitTrace(TraceKind::kPruned, why, 0, curr_idx, orig_b_pos, dest, 0, 0); \
    continue;                                                                \
  }
#else
#define SOKO_TRACE_EVENT(...) ((void)0)
#define SOKO_TRACE_PHASE(phase) ((void)0)
#define SOKO_PRUNE_IF(condition, why) \
  if (condition) continue;
#endif

namespace soko {
namespace {

std::string g_best_solution;

char* StrDup(const std::string& s) {
  char* p = static_cast<char*>(std::malloc(s.size() + 1));
  if (p) std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}

int DefaultThreadCount() {
  int n = 0;
#ifdef __APPLE__
  size_t len = sizeof(n);
  if (sysctlbyname("hw.perflevel0.physicalcpu", &n, &len, nullptr, 0) != 0) n = 0;
#elif defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  n = static_cast<int>(si.dwNumberOfProcessors);
#else
  n = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
#endif
  if (n < 1) n = 4;
  return std::min(n, kThreadCount);
}

std::vector<int> SelectedThreadIds() {
#ifdef SOKO_TRACE
  if (g_trace_thread_id >= 0) return {g_trace_thread_id};
#endif
  std::vector<int> ids;
  // SOKO_THREADS=0,4 picks thread configs explicitly (testing / few-core machines).
  if (const char* env = std::getenv("SOKO_THREADS")) {
    for (const char* p = env; *p;) {
      char* end = nullptr;
      const long id = std::strtol(p, &end, 10);
      if (end == p) break;
      if (id >= 0 && id < kThreadCount) ids.push_back(static_cast<int>(id));
      p = (*end == ',') ? end + 1 : end;
    }
    if (!ids.empty()) return ids;
  }
  const int max_threads = DefaultThreadCount();
  for (const int i : kDefaultThreadOrder) {
    if (static_cast<int>(ids.size()) >= max_threads) break;
    if (kThreadConfigs[i].enabled) ids.push_back(i);
  }
  if (ids.empty()) ids.push_back(0);
  return ids;
}

const ThreadConfig& ConfigFor(int thread_id) {
#ifdef SOKO_TRACE
  if (g_trace_config_override && thread_id == g_trace_thread_id) return g_trace_config;
#endif
  return kThreadConfigs[thread_id % kThreadCount];
}

void SetOutcome(int thread_id, ThreadOutcome outcome) {
  if (thread_id >= 0 && thread_id < kThreadSlots) {
    g_thread_outcome[thread_id].store(static_cast<u8>(outcome), std::memory_order_relaxed);
  }
}

void RunWorker(const Board& board, int thread_id) {
#ifdef __APPLE__
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
  SetOutcome(thread_id, ThreadOutcome::kRunning);

  if (ConfigFor(thread_id).backward) {
    const BackwardResult result = BackwardSearch(board, ConfigFor(thread_id), g_solution_found);
    g_nodes_explored.fetch_add(result.expansions, std::memory_order_relaxed);
    if (result.pool_exhausted) g_pool_exhausted.store(true);
    if (thread_id < kThreadSlots) g_thread_nodes[thread_id].store(result.expansions);
    SetOutcome(thread_id, result.solved           ? ThreadOutcome::kSolved
                          : result.pool_exhausted ? ThreadOutcome::kPoolFull
                          : g_solution_found.load() ? ThreadOutcome::kStopped
                                                    : ThreadOutcome::kExhausted);
    if (result.solved) {
      bool expected = false;
      if (g_solution_found.compare_exchange_strong(expected, true)) {
        g_winning_thread.store(thread_id);
        g_best_solution = result.moves;
        if (kDebugPrintWinner) {
          std::fprintf(stderr, "[solver] thread %d won (%zu moves, %llu nodes, backward)\n",
                       thread_id, g_best_solution.size(),
                       static_cast<unsigned long long>(result.expansions));
        }
      }
    }
    return;
  }

  std::unique_ptr<SearchWorker> solver;
  try {
    solver = std::make_unique<SearchWorker>(board, thread_id, ConfigFor(thread_id));
  } catch (const std::bad_alloc&) {
    SetOutcome(thread_id, ThreadOutcome::kNoMemory);
    return;
  }

  if (solver->InitiallySolved()) {
    SetOutcome(thread_id, ThreadOutcome::kSolved);
    bool expected = false;
    if (g_solution_found.compare_exchange_strong(expected, true)) {
      g_winning_thread.store(thread_id);
      g_best_solution.clear();
    }
    return;
  }

  if (solver->InitiallyHopeless()) {
    SetOutcome(thread_id, ThreadOutcome::kHopeless);
    return;
  }

  SetOutcome(thread_id, solver->Run(thread_id));
  if (std::getenv("SOKO_STATS")) solver->PrintStats(thread_id);
}

}  // namespace

SearchWorker::SearchWorker(const Board& board, int thread_id, const ThreadConfig& config)
    : config_(config),
      board_(board),
      reach_(board_),
      pool_(board_.box_count),
      table_(pool_, board_.box_count),
      room_(board_, config),
      heuristic_(board_, config, room_),
      deadlock_(board_, config, reach_) {
  prng_state_ = 123456789u + static_cast<u32>(thread_id) * 1337u;
  active_switch_penalty_ = config_.switch_penalty;

  pending_stride_words_ = pool_.stride() / sizeof(u64);
  pending_copy_bytes_ = sizeof(SokoState) + board_.box_count * sizeof(u16);
  pending_states_.assign(pending_stride_words_ * kMaxPendingChildren, 0);
}

void SearchWorker::PrintStats(int thread_id) const {
  const u64 slow = stat_slow_macro_ + stat_slow_sides_ + stat_slow_cut_vertex_;
  const u64 landed = stat_fast_path_ + slow;
  const double pct = landed ? 100.0 * static_cast<double>(stat_fast_path_) / landed : 0.0;
  const double dup_pct =
      stat_children_ ? 100.0 * static_cast<double>(stat_duplicates_) / stat_children_ : 0.0;
  std::fprintf(stderr,
               "[stats] T%d expansions=%llu children=%llu duplicates=%llu (%.1f%%) "
               "fast_path=%llu (%.1f%%) slow: macro=%llu sides=%llu cut_vertex=%llu "
               "table_slots=%u grows=%u\n",
               thread_id, static_cast<unsigned long long>(nodes_explored_),
               static_cast<unsigned long long>(stat_children_),
               static_cast<unsigned long long>(stat_duplicates_), dup_pct,
               static_cast<unsigned long long>(stat_fast_path_), pct,
               static_cast<unsigned long long>(stat_slow_macro_),
               static_cast<unsigned long long>(stat_slow_sides_),
               static_cast<unsigned long long>(stat_slow_cut_vertex_), table_.capacity(),
               table_.grow_count());
}

bool SearchWorker::InitiallyHopeless() const {
  if (!config_.dead_squares) return false;
  if (heuristic_.Calc(board_.initial_boxes.data()) >= kDeadHeuristic) return true;
  for (int i = 0; i < board_.box_count; ++i) {
    if (board_.min_dist[board_.initial_boxes[i]] >= kUnreach) return true;
  }
  return false;
}

std::string SearchWorker::ReconstructPath(u32 final_idx) {
  static constexpr char kPushChar[4] = {'u', 'd', 'l', 'r'};

  std::vector<u32> path;
  u32 curr = final_idx;
  while (curr != 1) {
    path.push_back(curr);
    curr = pool_.At(curr).prev_idx;
  }
  std::reverse(path.begin(), path.end());

  u16 p_pos = board_.initial_player;
  reach_.ResetBoxes();
  for (int i = 0; i < board_.box_count; ++i) reach_.SetBox(board_.initial_boxes[i], true);

  std::string solution;

  // Walks the player to `target` along a shortest path. Returns false when it cannot get there.
  auto walk_to = [&](u16 target) {
    if (!reach_.AppendWalk(p_pos, target, &solution)) return false;
    p_pos = target;
    return true;
  };
  auto push = [&](u16 box, int dir) {
    solution.push_back(kPushChar[dir]);
    reach_.SetBox(box, false);
    reach_.SetBox(static_cast<u16>(box + board_.dirs[dir]), true);
    p_pos = box;
  };

  for (const u32 step_idx : path) {
    const SokoState& s = pool_.At(step_idx);
    const int dir = s.push_dir & kDirMask;
    const bool room_place = (s.push_dir & kFlagRoomPlace) != 0;
    bool ok = true;

    if (s.push_dir & kFlagDelivery) {
      // Delivery macro: replan the moved box's trip to the room entry on this board.
      const SokoState& prev = pool_.At(s.prev_idx);
      u16 moved = 0;
      for (int i = 0; i < board_.box_count && !moved; ++i) {
        bool kept = false;
        for (int j = 0; j < board_.box_count; ++j) kept = kept || s.boxes[j] == prev.boxes[i];
        if (!kept) moved = prev.boxes[i];
      }
      reach_.FloodRegion<false>(p_pos);
      std::vector<std::pair<u16, u8>> pushes;
      ok = moved && room_.DeliveryPath(reach_, moved, &pushes, nullptr, nullptr);
      for (size_t k = 0; ok && k < pushes.size(); ++k) {
        ok = walk_to(static_cast<u16>(pushes[k].first - board_.dirs[pushes[k].second]));
        if (ok) push(pushes[k].first, pushes[k].second);
      }
    } else {
      const u16 line_end = room_place ? room_.entry() : s.last_dest;
      const u16 target =
          static_cast<u16>(line_end - (static_cast<int>(s.macro_steps) + 1) * board_.dirs[dir]);
      ok = walk_to(target);
      u16 box_curr = static_cast<u16>(target + board_.dirs[dir]);
      for (u8 step = 0; ok && step < s.macro_steps; ++step) {
        push(box_curr, dir);
        box_curr = static_cast<u16>(box_curr + board_.dirs[dir]);
      }
    }
    if (!ok) break;

    if (room_place) {
      std::string moves;
      u16 final_player = p_pos;
      if (!room_.ReplayPlacement(reach_, p_pos, s.last_dest, &moves, &final_player)) break;
      solution += moves;
      reach_.SetBox(room_.entry(), false);
      reach_.SetBox(s.last_dest, true);
      p_pos = final_player;
    }
  }

  return solution;
}

// Picks the outside boxes closest to the goals. Each one may try a delivery macro.
u32 SearchWorker::PickDeliveryCandidates(const SokoState& curr, u32 pi_corral_cid) const noexcept {
  u32 delivery_mask = 0;
  if (!room_.ok() || !config_.delivery_macro || pi_corral_cid != 0 ||
      room_.filled() >= room_.goal_count()) {
    return delivery_mask;
  }

  const bool gate_open = !reach_.Blocked(room_.gate());
  const u32 limit = config_.delivery_candidates ? config_.delivery_candidates : kMaxBoxes;
  for (u32 pick = 0; pick < limit; ++pick) {
    int best = -1;
    for (int i = 0; i < board_.box_count; ++i) {
      const u16 pos = curr.boxes[i];
      if (room_.InRoom(pos) || (delivery_mask & (1u << i))) continue;
      if (!gate_open && pos != room_.gate()) continue;  // A box in the gate blocks the rest.
      if (best < 0 || board_.min_dist[pos] < board_.min_dist[curr.boxes[best]]) best = i;
    }
    if (best < 0) break;
    delivery_mask |= 1u << best;
  }
  return delivery_mask;
}

SearchWorker::ExpandOutcome SearchWorker::ExpandNode(u32 curr_idx, SokoState& curr,
                                                     u32 pi_corral_cid, int thread_id,
                                                     u16 parent_norm) {
  const u32 delivery_mask = PickDeliveryCandidates(curr, pi_corral_cid);
  int pending_count = 0;

  for (int bi = 0; bi < board_.box_count; ++bi) {
    const u16 orig_b_pos = curr.boxes[bi];
    if (room_.ok() && room_.InRoom(orig_b_pos)) continue;  // Packed boxes never move again.

    // Direction 4 is the delivery macro: this box's whole trip to the goal-room entry.
    const int dir_count = ((delivery_mask & (1u << bi)) && orig_b_pos != room_.gate()) ? 5 : 4;
    for (int d = 0; d < dir_count; ++d) {
      const bool deliver = (d == 4);
      const u16 push_from =
          deliver ? orig_b_pos : static_cast<u16>(orig_b_pos - board_.dirs[d]);
      if (!deliver && !reach_.Reaches(push_from)) continue;

      u16 dest = deliver ? room_.entry() : static_cast<u16>(orig_b_pos + board_.dirs[d]);
      u8 macro_steps = 1;
      u8 push_dir = static_cast<u8>(d);
      int delivery_pushes = 0;

      // Goal room: the only push into it is gate -> entry, and it lands on the next goal.
      bool room_place = room_.ok() && dest == room_.entry();
      if (deliver) {
        u8 last_dir = 0;
        if (!room_.DeliveryPath(reach_, orig_b_pos, nullptr, &delivery_pushes, &last_dir)) continue;
        push_dir = static_cast<u8>(last_dir | kFlagDelivery);
      } else {
        if (room_.ok() && room_.InRoom(dest) &&
            (!room_place || room_.filled() >= room_.goal_count()))
          continue;

        SOKO_PRUNE_IF(pi_corral_cid > 0 && deadlock_.CorralId(dest) != pi_corral_cid,
                      TraceReason::kOutsideCorral)
        SOKO_PRUNE_IF(reach_.Blocked(dest) || (config_.dead_squares && board_.min_dist[dest] >= kUnreach),
                      reach_.Blocked(dest) ? TraceReason::kNone : TraceReason::kDeadSquare)
        SOKO_PRUNE_IF(!room_place && config_.two_by_two && deadlock_.Check2x2(dest, orig_b_pos),
                      TraceReason::kTwoByTwo)

        if (!room_place && config_.macro_moves && board_.tunnel_dest[dest][d] != dest) {
          bool clear = true;
          for (u16 scan = dest; scan != board_.tunnel_dest[dest][d];) {
            scan = static_cast<u16>(scan + board_.dirs[d]);
            if (reach_.IsBox(scan)) {
              clear = false;
              break;
            }
          }
          const u16 tunnel_end = board_.tunnel_dest[dest][d];
          if (clear && room_.ok() && room_.InRoom(tunnel_end)) {
            // A tunnel that runs into the room is only taken when it ends at the entry.
            if (tunnel_end == room_.entry() && room_.filled() < room_.goal_count()) {
              macro_steps = board_.tunnel_steps[dest][d];
              dest = tunnel_end;
              room_place = true;
            }
          } else if (clear) {
            macro_steps = board_.tunnel_steps[dest][d];
            dest = tunnel_end;
            SOKO_PRUNE_IF(config_.two_by_two && deadlock_.Check2x2(dest, orig_b_pos),
                          TraceReason::kTwoByTwo)
          }
        }

        if (!config_.freeze_check || room_place) {
        } else if (!board_.is_goal[dest]) {
          SOKO_PRUNE_IF(deadlock_.IsBoxFrozen(dest, dest, orig_b_pos), TraceReason::kFreeze)
        } else {
          bool adjacent_deadlock = false;
          for (int nd = 0; nd < 4; ++nd) {
            const u16 adj = static_cast<u16>(dest + board_.dirs[nd]);
            if (adj != orig_b_pos && reach_.IsBox(adj) && !board_.is_goal[adj]) {
              if (deadlock_.IsBoxFrozen(adj, dest, orig_b_pos)) {
                adjacent_deadlock = true;
                break;
              }
            }
          }
          if (!adjacent_deadlock && config_.goal_freeze_check &&
              deadlock_.IsBoxFrozen(dest, dest, orig_b_pos)) {
            adjacent_deadlock = deadlock_.HasDeadEmptyGoal(dest, orig_b_pos);
          }
          SOKO_PRUNE_IF(adjacent_deadlock, TraceReason::kFreeze)
        }
      }  // !deliver

      // Pass one builds the child in scratch space and prefetches its table slot. The table
      // insert happens in pass two, after every sibling is built, so the cache miss overlaps
      // with the flood fills of the other children.
      if (pool_.next_index() + pending_count + 1 >= static_cast<u32>(kStatePoolSize - 1) ||
          queue_.Full()) {
        return ExpandOutcome::kPoolExhausted;
      }

      SOKO_TRACE_PHASE(TracePhase::kPushChecks);
      u16 room_pushes = 0;
      if (room_place) {
        room_pushes = room_.push_len(room_.filled());
        dest = room_.order(room_.filled());
      }
      SokoState& next = PendingState(pending_count);

      next.prev_idx = curr_idx;
      next.push_dir = static_cast<u8>(push_dir | (room_place ? kFlagRoomPlace : 0));
      next.macro_steps = macro_steps;
      next.g = curr.g + (deliver ? delivery_pushes : macro_steps) + room_pushes;
      next.last_dest = dest;
      next.switches = curr.switches;
      if (curr.last_dest != 0 && curr.last_dest != orig_b_pos) ++next.switches;

      next.hash = curr.hash ^ board_.zobrist_box[orig_b_pos] ^ board_.zobrist_box[dest] ^
                  board_.zobrist_player[curr.normalized_player];

      SOKO_TRACE_PHASE(TracePhase::kStateFill);

      u16 next_norm;
      const bool dest_was_reachable = reach_.Reaches(dest);
      const int perp1 = ((d & kDirMask) < 2) ? 2 : 0;
      const int perp2 = ((d & kDirMask) < 2) ? 3 : 1;
      const u16 side1 = static_cast<u16>(orig_b_pos + board_.dirs[perp1]);
      const u16 side2 = static_cast<u16>(orig_b_pos + board_.dirs[perp2]);
      const bool sides_open = (reach_.Blocked(side1) || reach_.Reaches(side1)) &&
                              (reach_.Blocked(side2) || reach_.Reaches(side2));
      const bool is_macro = macro_steps > 1 || room_place;
      bool fast_path = sides_open && !is_macro;
      if (fast_path && dest_was_reachable) {
        fast_path = reach_.DestNotCutVertex(orig_b_pos, dest);
      }
      if (fast_path) {
        ++stat_fast_path_;
        const u16 region_min = (parent_norm != dest) ? parent_norm : reach_.second_norm();
        next_norm = std::min(region_min, orig_b_pos);
      } else {
        if (is_macro) {
          ++stat_slow_macro_;
        } else if (!sides_open) {
          ++stat_slow_sides_;
        } else {
          ++stat_slow_cut_vertex_;
        }
        reach_.SetBox(orig_b_pos, false);
        reach_.SetBox(dest, true);
        const u16 player_land =
            room_place ? room_.gate() : static_cast<u16>(dest - board_.dirs[d]);
        next_norm = reach_.NormalizeAfterPush(player_land);
        reach_.SetBox(orig_b_pos, true);
        reach_.SetBox(dest, false);
      }
      next.normalized_player = next_norm;
      next.hash ^= board_.zobrist_player[next_norm];
      SOKO_TRACE_PHASE(TracePhase::kPlayerFlood);

      table_.Prefetch(next.hash);

      int moved_dst_idx = 0;
      for (int i = 0; i < board_.box_count; ++i) {
        if (i != bi && curr.boxes[i] < dest) ++moved_dst_idx;
      }

      for (int dst = 0; dst < moved_dst_idx; ++dst) {
        const int src = (dst < bi) ? dst : dst + 1;
        next.boxes[dst] = curr.boxes[src];
      }

      next.boxes[moved_dst_idx] = dest;

      for (int dst = moved_dst_idx + 1; dst < board_.box_count; ++dst) {
        const int src = (dst <= bi) ? dst - 1 : dst;
        next.boxes[dst] = curr.boxes[src];
      }

      SOKO_TRACE_PHASE(TracePhase::kBoxCopy);

      PendingChild& pending = pending_[pending_count++];
      pending.orig_b_pos = orig_b_pos;
      pending.push_from = push_from;
      pending.box_index = static_cast<u8>(bi);
      pending.moved_dst_idx = static_cast<u8>(moved_dst_idx);
      pending.deliver = deliver;
    }
  }

  // Pass two: insert each child in generation order. Checking the table against `built`
  // avoids allocating and copying into StatePool for duplicates.
  for (int k = 0; k < pending_count; ++k) {
    const PendingChild& pending = pending_[k];
    [[maybe_unused]] const u16 orig_b_pos = pending.orig_b_pos;
    const int bi = pending.box_index;
    const int moved_dst_idx = pending.moved_dst_idx;
    const bool deliver = pending.deliver;
    const SokoState& built = PendingState(k);
    [[maybe_unused]] const u16 dest = built.last_dest;

    ++stat_children_;
    const u32 cand_idx = pool_.next_index();
    const bool fresh = table_.Insert(cand_idx, built);
    SOKO_TRACE_PHASE(TracePhase::kHashInsert);

    if (!fresh) {
      ++stat_duplicates_;
      SOKO_TRACE_EVENT(TraceKind::kPruned, TraceReason::kDuplicate, 0, curr_idx, orig_b_pos, dest,
                       0, 0);
      continue;
    }

    const u32 next_idx = pool_.Allocate();
    SokoState& next = pool_.At(next_idx);
    std::memcpy(&next, &built, pending_copy_bytes_);

    const u8* curr_goal_of = pool_.GoalOf(curr);
    u8* next_goal_of = pool_.GoalOf(next);
    for (int dst = 0; dst < moved_dst_idx; ++dst) {
      next_goal_of[dst] = curr_goal_of[(dst < bi) ? dst : dst + 1];
    }
    for (int dst = moved_dst_idx + 1; dst < board_.box_count; ++dst) {
      next_goal_of[dst] = curr_goal_of[(dst <= bi) ? dst - 1 : dst];
    }

    const int h = heuristic_.CalcIncremental(next.boxes, next_goal_of, moved_dst_idx, next.g);
    SOKO_TRACE_PHASE(TracePhase::kHeuristic);
    if (config_.dead_squares && h >= kDeadHeuristic) {
      SOKO_TRACE_EVENT(TraceKind::kPruned, TraceReason::kDeadHeuristic, 0, curr_idx, orig_b_pos,
                       dest, 0, 0);
      // Take the table entry back first, or its slot would point at a reused pool index.
      table_.UndoLastInsert();
      pool_.ReleaseLast();
      continue;
    }
    next.h = static_cast<u16>(std::min(h, 65535));

    if (h < best_h_seen_) {
      best_h_seen_ = h;
      stagnant_pops_ = 0;
    }

    const bool maybe_solved = (config_.heuristic == Heuristic::kNone) || (h == 0);
    if (maybe_solved && AllBoxesOnGoals(next.boxes)) {
      SOKO_TRACE_EVENT(TraceKind::kSolution, TraceReason::kNone, next_idx, curr_idx, orig_b_pos,
                       dest, next.g, h);
      std::string solution = ReconstructPath(next_idx);
      bool expected = false;
      if (g_solution_found.compare_exchange_strong(expected, true)) {
        g_winning_thread.store(thread_id);
        g_best_solution = std::move(solution);
        if (kDebugPrintWinner) {
          std::fprintf(stderr, "[solver] thread %d won (%zu moves, %llu nodes)\n", thread_id,
                       g_best_solution.size(),
                       static_cast<unsigned long long>(nodes_explored_));
        }
      }
      return ExpandOutcome::kSolutionFound;
    }

    const u32 noise = Xorshift32(&prng_state_) % 10'000;

    const u64 tier1 = static_cast<u64>(h) * config_.h_weight + noise * config_.noise_weight;
    // The switch penalty and g rate only change under bounded reheaping.
    const u64 tier2 = static_cast<u64>(next.switches) * active_switch_penalty_;
    const u64 tier3 = static_cast<u64>(next.g) * (config_.g_weight + g_rate_);
    const u64 tier4 =
        deliver ? 0 : static_cast<u64>(reach_.WalkDist(pending.push_from)) * config_.walk_weight;

    const u64 base_priority = tier1 + tier2 + tier3 + tier4 + (Xorshift32(&prng_state_) % 100);
    const u64 tie_breaker = 65535ULL - std::min<u64>(next.g, 65535);
    queue_.Push(next_idx, (base_priority << 16) | tie_breaker);
    SOKO_TRACE_EVENT(TraceKind::kChild, TraceReason::kNone, next_idx, curr_idx, orig_b_pos, dest,
                     next.g, h);
  }

  return ExpandOutcome::kContinue;
}

// Builds the root state: sorted boxes, hash, goal matching, and queue entry.
void SearchWorker::InitRoot() {
  SokoState& init_state = pool_.At(pool_.Allocate());
  init_state.prev_idx = 0;
  init_state.g = 0;
  init_state.hash = 0;
  init_state.switches = 0;
  init_state.last_dest = 0;

  for (int i = 0; i < board_.box_count; ++i) init_state.boxes[i] = board_.initial_boxes[i];

  for (int k = 1; k < board_.box_count; ++k) {
    const u16 key = init_state.boxes[k];
    int j = k - 1;
    while (j >= 0 && init_state.boxes[j] > key) {
      init_state.boxes[j + 1] = init_state.boxes[j];
      --j;
    }
    init_state.boxes[j + 1] = key;
  }

  for (int i = 0; i < board_.box_count; ++i) {
    init_state.hash ^= board_.zobrist_box[init_state.boxes[i]];
    reach_.SetBox(init_state.boxes[i], true);
  }

  const u16 init_norm = reach_.FloodRegion<false>(board_.initial_player);
  init_state.normalized_player = init_norm;
  init_state.hash ^= board_.zobrist_player[init_norm];

  best_h_seen_ = heuristic_.Calc(init_state.boxes, pool_.GoalOf(init_state));
  init_state.h = static_cast<u16>(std::min(best_h_seen_, 65535));
  stagnant_pops_ = 0;

  table_.Insert(1, init_state);
  queue_.Push(1, 0);

  PlaceBoxes(init_state, false);
}

// Full reheap: grow the weight of g so the search backs out of a deep, stuck branch.
void SearchWorker::ReheapFull(StagnationState* st) {
  constexpr u64 kFirstRate = 1'500'000ULL;
  constexpr u64 kLaterRate = 150'000ULL;

  if (st->reheap_count > 0 && best_h_seen_ < st->last_best_h) {
    st->last_best_h = best_h_seen_;
    st->reheap_count = 1;
    st->cumulative_g_rate = kFirstRate;
    queue_.Rebuild([&](const PQNode& node) {
      const SokoState& s = pool_.At(node.idx);
      const u64 base = static_cast<u64>(s.h) * config_.h_weight +
                       static_cast<u64>(s.switches) * config_.switch_penalty +
                       static_cast<u64>(s.g) * (config_.g_weight + st->cumulative_g_rate) +
                       (Xorshift32(&prng_state_) % 100);
      const u64 tie = 65535ULL - std::min<u64>(s.g, 65535);
      return (base << 16) | tie;
    });
  } else {
    st->last_best_h = best_h_seen_;
    ++st->reheap_count;
    const u64 delta_rate = (st->reheap_count == 1) ? kFirstRate : kLaterRate;
    st->cumulative_g_rate += delta_rate;
    queue_.Rebuild([&](const PQNode& node) {
      const u64 g = pool_.At(node.idx).g;
      const u64 penalty = (node.idx >= st->last_reheap_pool_ptr)
                              ? (g * st->cumulative_g_rate + 10'000'000ULL)
                              : (g * delta_rate);
      return node.priority + (penalty << 16);
    });
  }
  st->last_reheap_pool_ptr = pool_.next_index();
}

// Bounded reheap: rebuild all priorities in O(n) with a capped g weight.
void SearchWorker::ReheapBounded(StagnationState* st) {
  ++st->reheap_count;
  if (best_h_seen_ < st->last_best_h) {
    // The search made progress since the last rebuild. Back off to one step of g weight.
    g_rate_ = config_.g_rate_step;
  } else {
    g_rate_ = std::min(g_rate_ + config_.g_rate_step, config_.max_g_rate);
    active_switch_penalty_ =
        std::max(config_.switch_penalty_floor, active_switch_penalty_ * 3 / 4);
  }
  st->last_best_h = best_h_seen_;

  const u64 g_weight = config_.g_weight + g_rate_;
  queue_.Rebuild([&](const PQNode& node) {
    const SokoState& s = pool_.At(node.idx);
    const u64 base = static_cast<u64>(s.h) * config_.h_weight +
                     static_cast<u64>(s.switches) * active_switch_penalty_ +
                     static_cast<u64>(s.g) * g_weight + (Xorshift32(&prng_state_) % 100);
    return (base << 16) | (65535ULL - std::min<u64>(s.g, 65535));
  });
}

// Pocket sink: push the best entries behind the rest to leave a local minimum.
void SearchWorker::ReheapPocketSink(StagnationState* st) {
  queue_.SinkTop(st->pocket_buffer.data(), kPocketSinkLimit, pool_, [&](u32 idx, u64 priority) {
    const u64 g_penalty = (static_cast<u64>(pool_.At(idx).g) * 1'500'000ULL) << 16;
    return priority + g_penalty;
  });
}

void SearchWorker::Reheap(StagnationState* stagnation) {
  switch (config_.stagnation_mode) {
    case StagnationMode::kFullReheap: ReheapFull(stagnation); break;
    case StagnationMode::kBoundedReheap: ReheapBounded(stagnation); break;
    case StagnationMode::kPocketSink: ReheapPocketSink(stagnation); break;
    case StagnationMode::kNone: break;
  }
}

// Returns the cell the player stands on when the state is expanded.
u16 SearchWorker::PlayerBeforeExpansion(u32 curr_idx, const SokoState& curr) const noexcept {
  if (curr_idx == 1) return board_.initial_player;
  // After a goal-room placement the player can always walk back to the gate.
  if (curr.push_dir & kFlagRoomPlace) return room_.gate();
  return static_cast<u16>(curr.last_dest - board_.dirs[curr.push_dir & kDirMask]);
}

ThreadOutcome SearchWorker::Run(int thread_id) {
  struct NodeCountFlusher {
    SearchWorker* self;
    int thread_id;
    ~NodeCountFlusher() {
      if (self->nodes_explored_ > 0)
        g_nodes_explored.fetch_add(self->nodes_explored_, std::memory_order_relaxed);
      if (thread_id >= 0 && thread_id < kThreadSlots)
        g_thread_nodes[thread_id].store(self->nodes_explored_, std::memory_order_relaxed);
    }
  } node_count_flusher{this, thread_id};

  constexpr u32 kInitialStagnationLimit = 25'000;
  constexpr u32 kRecoveryStagnationLimit = 4'000;

  InitRoot();

  StagnationState stagnation;
  if (config_.stagnation_mode == StagnationMode::kPocketSink) {
    stagnation.pocket_buffer.resize(kPocketSinkLimit);
  }
  stagnation.last_reheap_pool_ptr = pool_.next_index();
  stagnation.last_best_h = best_h_seen_;
  int iter = 0;

  while (!queue_.empty()) {
    if ((iter++ & 127) == 0 && g_solution_found.load(std::memory_order_relaxed)) {
      return ThreadOutcome::kStopped;
    }

    // Every pool entry is in the table here, so the table can be rebuilt if it is half full.
    if (!table_.EnsureCapacity(pool_.next_index())) {
      g_pool_exhausted.store(true);
      return ThreadOutcome::kPoolFull;
    }

    const u32 curr_idx = queue_.Pop(pool_);
    SokoState& curr = pool_.At(curr_idx);
    ++nodes_explored_;

    u32 stag_limit =
        (stagnation.reheap_count == 0) ? kInitialStagnationLimit : kRecoveryStagnationLimit;
    if (config_.stagnation_mode == StagnationMode::kBoundedReheap &&
        stagnation.reheap_count > 0) {
      // Rebuilding costs O(queue size). Scale the interval to keep it a fixed share of pops.
      stag_limit = std::max<u32>(stag_limit, static_cast<u32>(queue_.size() / 16));
    }
    if (config_.stagnation_mode != StagnationMode::kNone && ++stagnant_pops_ >= stag_limit) {
      stagnant_pops_ = 0;
      Reheap(&stagnation);
    }

    SOKO_TRACE_EVENT(TraceKind::kPop, TraceReason::kNone, curr_idx, 0, 0, 0, 0, 0);

    PlaceBoxes(curr, true);
    if (room_.ok()) room_.CountFilled(curr.boxes);

    const u16 parent_norm = (config_.walk_weight != 0)
                                ? reach_.FloodRegion<true>(PlayerBeforeExpansion(curr_idx, curr))
                                : reach_.FloodRegion<false>(PlayerBeforeExpansion(curr_idx, curr));
    SOKO_TRACE_PHASE(TracePhase::kFloodFill);

    const bool has_unreachable_floor =
        (reach_.region_size() + board_.box_count < board_.playable_count);
    DeadlockDetector::PiCorralResult corral;
    if (config_.pi_corral && has_unreachable_floor) {
      corral = deadlock_.FindPiCorral();
      SOKO_TRACE_PHASE(TracePhase::kPiCorralSearch);
      if (corral.is_deadlock) {
        SOKO_TRACE_EVENT(TraceKind::kNodePruned, TraceReason::kPiCorralDeadlock, curr_idx, 0, 0, 0,
                         0, 0);
        PlaceBoxes(curr, false);
        continue;
      }
    }

    if (config_.hall_check && !deadlock_.HasFeasibleGoalAssignment(curr.boxes, pool_.GoalOf(curr))) {
      SOKO_TRACE_EVENT(TraceKind::kNodePruned, TraceReason::kHallInfeasible, curr_idx, 0, 0, 0, 0,
                       0);
      PlaceBoxes(curr, false);
      continue;
    }
    SOKO_TRACE_PHASE(TracePhase::kHallCheck);

    const ExpandOutcome outcome =
        ExpandNode(curr_idx, curr, corral.corral_id, thread_id, parent_norm);
    if (outcome == ExpandOutcome::kPoolExhausted) {
      g_pool_exhausted.store(true);
      return ThreadOutcome::kPoolFull;
    }
    if (outcome == ExpandOutcome::kSolutionFound) return ThreadOutcome::kSolved;

    PlaceBoxes(curr, false);
  }
  return ThreadOutcome::kExhausted;
}

}  // namespace soko

char* SolveSokoban(int width, int height, char** map, char** items) {
  soko::g_best_solution.clear();
  g_solution_found.store(false);
  g_nodes_explored.store(0);
  g_winning_thread.store(-1);
  g_pool_exhausted.store(false);
  for (int t = 0; t < kThreadSlots; ++t) {
    g_thread_nodes[t].store(0);
    g_thread_outcome[t].store(static_cast<u8>(ThreadOutcome::kNotRun));
  }

  const std::vector<int> thread_ids = soko::SelectedThreadIds();
  bool need_tunnels = false;
  for (int id : thread_ids) {
    const ThreadConfig& cfg = soko::ConfigFor(id);
    if (!cfg.backward && cfg.macro_moves) {
      need_tunnels = true;
      break;
    }
  }

  std::unique_ptr<soko::Board> board;
  try {
    board = std::make_unique<soko::Board>(width, height, map, items, need_tunnels);
  } catch (const std::bad_alloc&) {
    for (int id : thread_ids) soko::SetOutcome(id, ThreadOutcome::kNoMemory);
    return soko::StrDup("");
  }

  std::vector<std::thread> threads;
  threads.reserve(thread_ids.size());
  for (int id : thread_ids) {
    threads.emplace_back(soko::RunWorker, std::cref(*board), id);
  }
  for (auto& t : threads) t.join();

  return soko::StrDup(soko::g_best_solution);
}
