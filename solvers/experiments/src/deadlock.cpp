#include "soko/deadlock.h"

#include <algorithm>
#include <array>

namespace soko {

DeadlockDetector::DeadlockDetector(const Board& board, const ThreadConfig& config,
                                   Reachability& reach)
    : board_(board), config_(config), reach_(reach) {
  std::fill_n(freeze_mark_.data(), board_.area, 0);
  std::fill_n(corral_id_.data(), board_.area, 0);
  BuildFreezeStaticTable();
}

// A cell is statically frozen on an axis when a wall touches it on that axis,
// or when both neighbours on that axis are dead squares.
void DeadlockDetector::BuildFreezeStaticTable() {
  std::fill_n(freeze_static_.data(), board_.area, 0);
  for (int i = 0; i < board_.playable_count; ++i) {
    const u16 pos = board_.playable_cells[i];
    u8 mask = 0;
    for (int axis = 0; axis < 2; ++axis) {
      const int d = axis ? board_.stride : 1;
      const u16 a = static_cast<u16>(pos - d);
      const u16 b = static_cast<u16>(pos + d);
      const bool wall_side = (board_.IsWall(a) || board_.IsWall(b));
      const bool dead_both = (board_.min_dist[a] >= kUnreach && board_.min_dist[b] >= kUnreach);
      if (wall_side || dead_both) mask |= static_cast<u8>(1u << axis);
    }
    freeze_static_[pos] = mask;
  }
}

bool DeadlockDetector::FreezeFrozen(u16 pos, u16 new_b, u16 old_b) noexcept {
  return FreezeBlocked(pos, 0, new_b, old_b) && FreezeBlocked(pos, 1, new_b, old_b);
}

bool DeadlockDetector::FreezeBlocked(u16 pos, int axis, u16 new_b, u16 old_b) noexcept {
  if (freeze_static_[pos] & (1u << axis)) return true;

  const int d = axis ? board_.stride : 1;
  const u16 a = static_cast<u16>(pos - d);
  const u16 b = static_cast<u16>(pos + d);

  freeze_mark_[pos] = freeze_seq_;

  const bool box_a = HasBox(a, new_b, old_b);
  const bool box_b = HasBox(b, new_b, old_b);

  if (box_a && (freeze_mark_[a] == freeze_seq_ || FreezeFrozen(a, new_b, old_b))) return true;
  if (box_b && (freeze_mark_[b] == freeze_seq_ || FreezeFrozen(b, new_b, old_b))) return true;

  return false;
}

bool DeadlockDetector::HasDeadEmptyGoal(u16 new_b, u16 old_b) noexcept {
  for (int g = 0; g < board_.goal_count; ++g) {
    const u16 goal_pos = board_.goals[g];
    if (HasBox(goal_pos, new_b, old_b)) continue;

    bool can_enter = false;
    for (int d = 0; d < 4; ++d) {
      const u16 from = static_cast<u16>(goal_pos - board_.dirs[d]);
      const u16 stand = static_cast<u16>(from - board_.dirs[d]);
      if (board_.IsWall(from) || board_.IsWall(stand)) continue;
      if (HasBox(from, new_b, old_b) && IsBoxFrozen(from, new_b, old_b)) continue;
      if (HasBox(stand, new_b, old_b) && IsBoxFrozen(stand, new_b, old_b)) continue;
      can_enter = true;
      break;
    }
    if (!can_enter) return true;
  }
  return false;
}

DeadlockDetector::PiCorralResult DeadlockDetector::FindPiCorral() {
  PiCorralResult result;
  u16* queue = reach_.queue();

  if (corral_seq_ > 0xFF000000u) {
    std::fill_n(corral_id_.data(), board_.area, 0);
    corral_seq_ = 0;
  }
  corral_seq_ += kMaxCells;
  int num_corrals = 0;

  int corral_cells_left = board_.playable_count - reach_.region_size() - board_.box_count;
  if (corral_cells_left <= 0) return result;

  int best_inward_count = 1'000'000;

  for (int pi = 0; pi < board_.playable_count; ++pi) {
    const int i = board_.playable_cells[pi];
    if (reach_.Blocked(i) || reach_.Reaches(i)) continue;
    if (corral_id_[i] >= corral_seq_) continue;

    ++num_corrals;
    const u32 cid = corral_seq_ + num_corrals;
    int c_head = 0, c_tail = 0;
    queue[c_tail++] = static_cast<u16>(i);
    corral_id_[i] = cid;

    bool has_goal = false;
    int db_count = 0;
    std::array<u16, kMaxBoxes> db{};

    while (c_head < c_tail) {
      const u16 c = queue[c_head++];
      if (board_.is_goal[c]) has_goal = true;

      for (int d = 0; d < 4; ++d) {
        const u16 n = static_cast<u16>(c + board_.dirs[d]);
        if (board_.IsWall(n)) continue;

        if (reach_.IsBox(n)) {
          bool found = false;
          for (int b = 0; b < db_count; ++b) {
            if (db[b] == n) {
              found = true;
              break;
            }
          }
          if (!found && db_count < board_.box_count) db[db_count++] = n;
        } else if (corral_id_[n] != cid) {
          if (!reach_.Reaches(n)) {
            corral_id_[n] = cid;
            queue[c_tail++] = n;
          }
        }
      }
    }

    corral_cells_left -= c_tail;

    bool needs_work = has_goal;
    if (!needs_work) {
      for (int b = 0; b < db_count; ++b) {
        if (!board_.is_goal[db[b]]) {
          needs_work = true;
          break;
        }
      }
    }
    if (!needs_work) {
      if (corral_cells_left <= 0) break;
      continue;
    }

    bool is_pi = true;
    int valid_inward_count = 0;

    auto on_barrier = [&](u16 cell) {
      for (int k = 0; k < db_count; ++k) {
        if (db[k] == cell) return true;
      }
      return false;
    };
    // True if every push of `box` either stands on / pushes into a barrier box, hits a
    // wall, or lands on a dead square: it can't move until the barrier itself moves.
    auto stuck_behind_barrier = [&](u16 box) {
      for (int k = 0; k < 4; ++k) {
        const u16 to = static_cast<u16>(box + board_.dirs[k]);
        const u16 stand = static_cast<u16>(box - board_.dirs[k]);
        const bool to_bad = board_.IsWall(to) || on_barrier(to) || board_.min_dist[to] >= kUnreach;
        const bool stand_bad = board_.IsWall(stand) || on_barrier(stand);
        if (!to_bad && !stand_bad) return false;
      }
      return true;
    };

    for (int b = 0; b < db_count && is_pi; ++b) {
      const u16 B = db[b];
      for (int d = 0; d < 4; ++d) {
        const u16 to = static_cast<u16>(B + board_.dirs[d]);
        const u16 from = static_cast<u16>(B - board_.dirs[d]);
        const bool to_is_empty = !reach_.Blocked(to);
        const bool from_reach = (reach_.Reaches(from));

        if (to_is_empty && from_reach) {
          if (corral_id_[to] != cid) {
            is_pi = false;
            break;
          } else if (!config_.aggressive_corral) {
            ++valid_inward_count;
          } else {
            bool dead_push = (config_.dead_squares && board_.min_dist[to] >= kUnreach) ||
                             (config_.two_by_two && Check2x2(to, B));
            if (!dead_push && config_.freeze_check && !board_.is_goal[to]) {
              dead_push = IsBoxFrozen(to, to, B);
            }
            if (!dead_push) ++valid_inward_count;
          }
        }

        // A push onto a dead square can't help however the player gets there, so it can't
        // stop the corral from being PI. In the sound variant every other push the player
        // may make later (from a cell outside its region) counts too, not only pushes into
        // this corral, since such a push could move the barrier before the corral is
        // entered. corral_optimistic skips that: faster, but it can call a solvable state
        // dead, so the thread is no longer complete (other threads still cover it).
        const bool dead_target = config_.corral_stuck_boxes && board_.min_dist[to] >= kUnreach;
        const bool counts = corral_id_[to] == cid ||
                            (config_.corral_stuck_boxes && !config_.corral_optimistic);
        if (to_is_empty && counts && !dead_target) {
          if (!from_reach && !board_.IsWall(from)) {
            if (reach_.IsBox(from)) {
              bool from_in_db = false;
              for (int k = 0; k < 4; ++k) {
                if (corral_id_[static_cast<u16>(from + board_.dirs[k])] == cid) {
                  from_in_db = true;
                  break;
                }
              }
              if (!from_in_db &&
                  (!config_.aggressive_corral || !config_.freeze_check || !IsBoxFrozen(from, 0, 0)) &&
                  !(config_.corral_stuck_boxes && stuck_behind_barrier(from))) {
                is_pi = false;
                break;
              }
            } else if (corral_id_[from] != cid) {
              is_pi = false;
              break;
            }
          }
        }
      }
    }

    if (is_pi) {
      if (valid_inward_count == 0) {
        result.is_deadlock = true;
        return result;
      }
      if (!config_.aggressive_corral) {
        result.corral_id = cid;
        return result;
      }
      if (valid_inward_count < best_inward_count) {
        best_inward_count = valid_inward_count;
        result.corral_id = cid;
      }
    }
    if (corral_cells_left <= 0) break;
  }
  return result;
}

bool DeadlockDetector::TryAugmentMatch(const u16* boxes, const std::array<u8, kMaxBoxes>& unmatched_b,
                             const std::array<u8, kMaxBoxes>& unmatched_g, int ug_count,
                             int box_idx, std::array<int, kMaxBoxes>& goal_match,
                             std::array<bool, kMaxBoxes>& goal_visited) const {
  const u16 box_pos = boxes[unmatched_b[box_idx]];
  for (int gi = 0; gi < ug_count; ++gi) {
    if (goal_visited[gi]) continue;
    if (board_.goal_dist[box_pos][unmatched_g[gi]] >= kUnreach) continue;
    goal_visited[gi] = true;
    if (goal_match[gi] == -1 ||
        TryAugmentMatch(boxes, unmatched_b, unmatched_g, ug_count, goal_match[gi], goal_match,
                        goal_visited)) {
      goal_match[gi] = box_idx;
      return true;
    }
  }
  return false;
}

bool DeadlockDetector::HasFeasibleGoalAssignment(const u16* boxes, const u8* cached_goal_of) const {
  if (cached_goal_of) {
    bool fully_assigned = true;
    for (int b = 0; b < board_.box_count; ++b) {
      if (cached_goal_of[b] == 0xFF) {
        fully_assigned = false;
        break;
      }
    }
    if (fully_assigned) return true;
  }

  u32 used_g = 0;
  u32 used_b = 0;
  for (int b = 0; b < board_.box_count; ++b) {
    const u8 g = board_.goal_idx[boxes[b]];
    if (g != 0xFF) {
      used_g |= 1u << g;
      used_b |= 1u << b;
    }
  }

  std::array<u8, kMaxBoxes> unmatched_b{};
  std::array<u8, kMaxBoxes> unmatched_g{};
  int ub_count = 0, ug_count = 0;
  for (int b = 0; b < board_.box_count; ++b) {
    if (!(used_b & (1u << b))) unmatched_b[ub_count++] = static_cast<u8>(b);
  }
  for (int g = 0; g < board_.goal_count; ++g) {
    if (!(used_g & (1u << g))) unmatched_g[ug_count++] = static_cast<u8>(g);
  }

  std::array<int, kMaxBoxes> goal_match;
  goal_match.fill(-1);

  for (int bi = 0; bi < ub_count; ++bi) {
    std::array<bool, kMaxBoxes> goal_visited{};
    if (!TryAugmentMatch(boxes, unmatched_b, unmatched_g, ug_count, bi, goal_match, goal_visited))
      return false;
  }
  return true;
}


}  // namespace soko
