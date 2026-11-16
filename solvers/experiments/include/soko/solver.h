// Best-first search worker. One worker runs one thread configuration of the portfolio.
#ifndef NATIVE_INCLUDE_SOKO_SOLVER_H_
#define NATIVE_INCLUDE_SOKO_SOLVER_H_

#include <string>
#include <vector>

#include "flags.h"
#include "sokoban.h"
#include "soko/board.h"
#include "soko/deadlock.h"
#include "soko/goal_room.h"
#include "soko/heuristic.h"
#include "soko/priority_queue.h"
#include "soko/reachability.h"
#include "soko/state_pool.h"
#include "soko/types.h"

namespace soko {

class SearchWorker {
 public:
  SearchWorker(int width, int height, char** map, char** items, int thread_id,
               const ThreadConfig& config);

  bool InitiallySolved() const noexcept { return AllBoxesOnGoals(board_.initial_boxes.data()); }
  bool InitiallyHopeless() const;

  void Run(int thread_id);

 private:
  static constexpr int kPocketSinkLimit = 16'384;

  enum class ExpandOutcome { kContinue, kSolutionFound, kPoolExhausted };

  // State of the stagnation controller. It lives across expansions inside Run.
  struct StagnationState {
    u32 last_reheap_pool_ptr = 0;
    u64 reheap_count = 0;
    u64 cumulative_g_rate = 0;
    int last_best_h = 0;
    std::vector<PQNode> pocket_buffer;
  };

  void InitRoot();
  void Reheap(StagnationState* stagnation);
  void ReheapFull(StagnationState* stagnation);
  void ReheapBounded(StagnationState* stagnation);
  void ReheapPocketSink(StagnationState* stagnation);
  u16 PlayerBeforeExpansion(u32 curr_idx, const SokoState& curr) const noexcept;
  u32 PickDeliveryCandidates(const SokoState& curr, u32 pi_corral_cid) const noexcept;
  ExpandOutcome ExpandNode(u32 curr_idx, SokoState& curr, u32 pi_corral_cid, int thread_id,
                           u16 parent_norm);
  std::string ReconstructPath(u32 final_idx);

  bool AllBoxesOnGoals(const u16* boxes) const noexcept {
    for (int i = 0; i < board_.box_count; ++i) {
      if (board_.goal_idx[boxes[i]] == kNoGoal) return false;
    }
    return true;
  }

  void PlaceBoxes(const SokoState& s, bool value) noexcept {
    for (int i = 0; i < board_.box_count; ++i) reach_.SetBox(s.boxes[i], value);
  }

#ifdef SOKO_TRACE
  void EmitTrace(TraceKind kind, TraceReason reason, u32 node, u32 parent, u16 from, u16 to,
                 u32 g, u32 h) const {
    if (!g_trace_event) return;
    if (kind == TraceKind::kPruned && reason == TraceReason::kNone) return;

    g_trace_event({kind, reason, node, parent, board_.row_of[from] - 2, board_.col_of[from] - 2,
                   board_.row_of[to] - 2, board_.col_of[to] - 2, g, h});
  }
#endif

  const ThreadConfig& config_;
  Board board_;
  Reachability reach_;
  StatePool pool_;
  TranspositionTable table_;
  PriorityQueue queue_;
  GoalRoom room_;
  HeuristicEngine heuristic_;
  DeadlockDetector deadlock_;

  u32 prng_state_ = 0;
  u64 nodes_explored_ = 0;
  int best_h_seen_ = 1'000'000;
  u32 stagnant_pops_ = 0;
  u64 active_switch_penalty_ = 0;
  u64 g_rate_ = 0;
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_SOLVER_H_
