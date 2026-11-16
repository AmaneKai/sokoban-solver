// Lower-bound estimate of the pushes still needed to finish a state.
#ifndef NATIVE_INCLUDE_SOKO_HEURISTIC_H_
#define NATIVE_INCLUDE_SOKO_HEURISTIC_H_

#include <cstdlib>

#include "flags.h"
#include "soko/board.h"
#include "soko/goal_room.h"
#include "soko/types.h"

namespace soko {

class HeuristicEngine {
 public:
  HeuristicEngine(const Board& board, const ThreadConfig& config, const GoalRoom& room)
      : board_(board), config_(config), room_(room) {}

  // Push distance from `pos` to goal `goal`.
  int Dist(u16 pos, int goal) const noexcept {
    switch (config_.heuristic) {
      case Heuristic::kManhattan: {
        const u16 g = board_.goals[goal];
        return std::abs(board_.row_of[pos] - board_.row_of[g]) +
               std::abs(board_.col_of[pos] - board_.col_of[g]);
      }
      case Heuristic::kNone:
        return 0;
      case Heuristic::kReverseBfs:
        break;
    }
    const int d = board_.goal_dist[pos][goal];
    return (d >= kUnreach && !config_.dead_squares) ? kSoftUnreach : d;
  }

  // Full estimate. Fills `out_goal_of` with the matched goal of each box when given.
  int Calc(const u16* boxes, u8* out_goal_of = nullptr) const;

  // Cheaper estimate after one box moved. Falls back to Calc on a fixed interval.
  int CalcIncremental(const u16* boxes, u8* goal_of, int moved_idx, u16 depth) const;

  // Minimum-cost matching of n boxes to m goals. Returns -1 when no matching exists.
  static int HungarianMinCost(int n, int m, const int cost[kMaxBoxes][kMaxBoxes], u8* out_match);
  static int GreedyMinCost(int n, int m, const int cost[kMaxBoxes][kMaxBoxes], u8* out_match);

 private:
  int LinearConflictPenalty(const u16* boxes, const u8* goal_of) const;

  const Board& board_;
  const ThreadConfig& config_;
  const GoalRoom& room_;
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_HEURISTIC_H_
