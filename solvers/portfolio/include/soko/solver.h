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
  SearchWorker(const Board& board, int thread_id, const ThreadConfig& config);

  bool InitiallySolved() const noexcept { return AllBoxesOnGoals(board_.initial_boxes.data()); }
  bool InitiallyHopeless() const;

  ThreadOutcome Run(int thread_id);

  // One-line summary of the counters below. Printed when SOKO_STATS is set.
  void PrintStats(int thread_id) const;

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

  // A child that passed the push checks and is waiting for its table insert. Its SokoState
  // lives in the scratch buffer until the insert says it is new.
  struct PendingChild {
    u16 orig_b_pos;
    u16 push_from;
    u8 box_index;
    u8 moved_dst_idx;
    bool deliver;
  };
  static constexpr int kMaxPendingChildren = 5 * kMaxBoxes;

  SokoState& PendingState(int k) noexcept {
    return *reinterpret_cast<SokoState*>(pending_states_.data() +
                                         static_cast<size_t>(k) * pending_stride_words_);
  }

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
  const Board& board_;
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

  // Scratch space for the children of the node being expanded.
  std::vector<u64> pending_states_;
  size_t pending_stride_words_ = 0;
  size_t pending_copy_bytes_ = 0;
  PendingChild pending_[kMaxPendingChildren];

  // Counters for PrintStats.
  u64 stat_children_ = 0;
  u64 stat_duplicates_ = 0;
  u64 stat_fast_path_ = 0;
  u64 stat_slow_macro_ = 0;
  u64 stat_slow_sides_ = 0;
  u64 stat_slow_cut_vertex_ = 0;
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_SOLVER_H_
