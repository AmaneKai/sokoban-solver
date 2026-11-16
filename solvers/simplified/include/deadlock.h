#ifndef DEADLOCK_H_
#define DEADLOCK_H_

#include <array>
#include <cstdint>
#include "board.h"

struct PiCorralResult {
  uint32_t corral_id = 0;
  bool is_deadlock = false;
};

class DeadlockDetector {
 public:
  DeadlockDetector();
  void Reset();

  bool Check2x2(const Board& board, int b, int old_b) const;
  bool IsFrozen(const Board& board, int pos, int new_box, int old_box);
  bool DestNotCutVertex(const Board& board, int dest, const std::array<uint32_t, kMaxCells>& reach,
                        uint32_t reach_seq) const;

  PiCorralResult FindPiCorral(const Board& board, int region_size,
                              const std::array<uint32_t, kMaxCells>& reach, uint32_t reach_seq);

  bool HasFeasibleGoalAssignment(const Board& board, const std::array<int, kMaxBoxes>& boxes,
      const std::array<int, kMaxBoxes>& cached_goal_of) const;

  uint32_t corral_id(int cell) const { return corral_id_[cell]; }

 private:
  bool CheckFrozen(const Board& board, int pos, int new_box, int old_box);
  bool IsBlockedAxis(const Board& board, int pos, int axis, int new_box,int old_box);
  bool TryAugmentMatch(const Board& board, const std::array<int, kMaxBoxes>& boxes,
                       const std::array<int, kMaxBoxes>& unmatched_b,
                       const std::array<int, kMaxBoxes>& unmatched_g,int ug_count, int box_idx,
                       std::array<int, kMaxBoxes>& goal_match,
                       std::array<bool, kMaxBoxes>& goal_visited) const;

  std::array<bool, 256> cut_vertex_table_{};
  std::array<uint32_t, kMaxCells> frz_mark_{};
  uint32_t frz_seq_ = 0;
  std::array<uint32_t, kMaxCells> corral_id_{};
  uint32_t corral_seq_ = 0;
  std::array<int, kMaxCells> bfs_q_{};
};

#endif  // DEADLOCK_H_
