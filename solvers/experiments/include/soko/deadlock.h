// Deadlock detection: freeze tables, 2x2 blocks, PI-corrals, and Hall feasibility.
#ifndef NATIVE_INCLUDE_SOKO_DEADLOCK_H_
#define NATIVE_INCLUDE_SOKO_DEADLOCK_H_

#include <array>

#include "flags.h"
#include "soko/board.h"
#include "soko/reachability.h"
#include "soko/types.h"

namespace soko {

class DeadlockDetector {
 public:
  struct PiCorralResult {
    u32 corral_id = 0;
    bool is_deadlock = false;
  };

  DeadlockDetector(const Board& board, const ThreadConfig& config, Reachability& reach);

  // True when a box at `b` completes a 2x2 block of walls and boxes that cannot be cleared.
  bool Check2x2(u16 b, u16 old_b) const noexcept {
    const int w = board_.stride;
    const std::array<int, 4> c1_off = {-1, 1, -1, 1};
    const std::array<int, 4> c2_off = {-w, -w, w, w};
    const std::array<int, 4> c3_off = {-1 - w, 1 - w, -1 + w, 1 + w};

    for (int q = 0; q < 4; ++q) {
      const u16 c1 = static_cast<u16>(b + c1_off[q]);
      const u16 c2 = static_cast<u16>(b + c2_off[q]);
      const u16 c3 = static_cast<u16>(b + c3_off[q]);
      if (IsBlocked(c1, b, old_b) && IsBlocked(c2, b, old_b) && IsBlocked(c3, b, old_b)) {
        if (!board_.is_goal[b]) return true;
        if (!board_.IsWall(c1) && !board_.is_goal[c1]) return true;
        if (!board_.IsWall(c2) && !board_.is_goal[c2]) return true;
        if (!board_.IsWall(c3) && !board_.is_goal[c3]) return true;
      }
    }
    return false;
  }

  // True when the box at `pos` can never move, given the box move from `old_b` to `new_b`.
  bool IsBoxFrozen(u16 pos, u16 new_b, u16 old_b) noexcept {
    if (freeze_seq_ > 0xFF000000u) {
      std::fill_n(freeze_mark_.data(), board_.area, 0);
      freeze_seq_ = 0;
    }
    ++freeze_seq_;
    return FreezeFrozen(pos, new_b, old_b);
  }

  // True when some empty goal can no longer receive a box.
  bool HasDeadEmptyGoal(u16 new_b, u16 old_b) noexcept;

  // Finds the best PI-corral (a region the player cannot enter without pushing a barrier box).
  PiCorralResult FindPiCorral();

  // Hall check: true when every box can still reach a distinct goal.
  bool HasFeasibleGoalAssignment(const u16* boxes, const u8* cached_goal_of = nullptr) const;

  u32 CorralId(u16 cell) const noexcept { return corral_id_[cell]; }

 private:
  bool HasBox(u16 cell, u16 new_b, u16 old_b) const noexcept {
    return (cell == new_b) || (cell != old_b && reach_.IsBox(cell));
  }
  bool IsBlocked(u16 cell, u16 b, u16 old_b) const noexcept {
    if (cell == b) return true;
    if (cell == old_b) return false;
    return reach_.Blocked(cell);
  }

  void BuildFreezeStaticTable();
  bool FreezeBlocked(u16 pos, int axis, u16 new_b, u16 old_b) noexcept;
  bool FreezeFrozen(u16 pos, u16 new_b, u16 old_b) noexcept;
  bool TryAugmentMatch(const u16* boxes, const std::array<u8, kMaxBoxes>& unmatched_b,
                       const std::array<u8, kMaxBoxes>& unmatched_g, int ug_count, int box_idx,
                       std::array<int, kMaxBoxes>& goal_match,
                       std::array<bool, kMaxBoxes>& goal_visited) const;

  const Board& board_;
  const ThreadConfig& config_;
  Reachability& reach_;

  std::array<u32, kMaxCells> freeze_mark_;
  std::array<u8, kMaxCells> freeze_static_;
  u32 freeze_seq_ = 0;
  std::array<u32, kMaxCells> corral_id_;
  u32 corral_seq_ = 0;
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_DEADLOCK_H_
