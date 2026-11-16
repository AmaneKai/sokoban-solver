#include "deadlock.h"

DeadlockDetector::DeadlockDetector() {
  for (int mask = 0; mask < 256; ++mask) {
    int ortho_count = 0;
    for (int i = 0; i < 8; i += 2) {
      if (mask & (1 << i)) ++ortho_count;
    }
    if (ortho_count <= 1) {
      cut_vertex_table_[mask] = true;
      continue;
    }

    int start = -1;
    for (int i = 0; i < 8; i += 2) {
      if (mask & (1 << i)) {
        start = i;
        break;
      }
    }

    unsigned spanned = 1u << start;
    for (int k = 1; k < 8; ++k) {
      const int i = (start + k) & 7;
      if (!(mask & (1 << i))) break;
      spanned |= 1u << i;
    }
    for (int k = 1; k < 8; ++k) {
      const int i = (start + 8 - k) & 7;
      if (!(mask & (1 << i))) break;
      spanned |= 1u << i;
    }

    bool ok = true;
    for (int i = 0; i < 8; i += 2) {
      if ((mask & (1 << i)) && !(spanned & (1u << i))) {
        ok = false;
        break;
      }
    }
    cut_vertex_table_[mask] = ok;
  }

  Reset();
}

void DeadlockDetector::Reset() {
  frz_mark_.fill(0);
  corral_id_.fill(0);
  frz_seq_ = 0;
  corral_seq_ = 0;
}

bool DeadlockDetector::Check2x2(const Board& board, int b, int old_b) const {
  const int w = board.width();
  const std::array<int, 4> c1_off = {-1, 1, -1, 1};
  const std::array<int, 4> c2_off = {-w, -w, w, w};
  const std::array<int, 4> c3_off = {-1 - w, 1 - w, -1 + w, 1 + w};

  auto IsBlocked = [&](int cell) {
    if (cell == b) return true;
    if (cell == old_b) return false;
    return board.blocked(cell);
  };

  for (int q = 0; q < 4; ++q) {
    const int c1 = b + c1_off[q];
    const int c2 = b + c2_off[q];
    const int c3 = b + c3_off[q];

    if (IsBlocked(c1) && IsBlocked(c2) && IsBlocked(c3)) {
      if (!board.is_goal(b)) return true;
      if (board.board(c1) != '#' && !board.is_goal(c1)) return true;
      if (board.board(c2) != '#' && !board.is_goal(c2)) return true;
      if (board.board(c3) != '#' && !board.is_goal(c3)) return true;
    }
  }

  return false;
}

bool DeadlockDetector::IsBlockedAxis(const Board& board, int pos, int axis,
                                     int new_box, int old_box) {
  if (board.frz_static(pos) & (1u << axis)) return true;

  const int d = (axis == 0) ? 1 : board.width();
  const int a = pos - d;
  const int b = pos + d;

  auto HasBox = [&](int cell) {
    return (cell == new_box) || (cell != old_box && board.is_box(cell));
  };

  frz_mark_[pos] = frz_seq_;

  if (HasBox(a) && (frz_mark_[a] == frz_seq_ ||
                    CheckFrozen(board, a, new_box, old_box))) {
    return true;
  }
  if (HasBox(b) && (frz_mark_[b] == frz_seq_ ||
                    CheckFrozen(board, b, new_box, old_box))) {
    return true;
  }

  return false;
}

bool DeadlockDetector::CheckFrozen(const Board& board, int pos, int new_box,
                                   int old_box) {
  return IsBlockedAxis(board, pos, 0, new_box, old_box) &&
         IsBlockedAxis(board, pos, 1, new_box, old_box);
}

bool DeadlockDetector::IsFrozen(const Board& board, int pos, int new_box,
                               int old_box) {
  if (frz_seq_ > 0xFF000000u) {
    frz_mark_.fill(0);
    frz_seq_ = 0;
  }
  ++frz_seq_;
  return CheckFrozen(board, pos, new_box, old_box);
}

bool DeadlockDetector::DestNotCutVertex(
    const Board& board, int dest, const std::array<uint32_t, kMaxCells>& reach,
    uint32_t reach_seq) const {
  unsigned mask = 0;
  for (int i = 0; i < 8; ++i) {
    const bool in = (reach[dest + board.ring_offset(i)] == reach_seq);
    mask |= static_cast<unsigned>(in) << i;
  }
  return cut_vertex_table_[mask];
}

PiCorralResult DeadlockDetector::FindPiCorral(
    const Board& board, int region_size,
    const std::array<uint32_t, kMaxCells>& reach, uint32_t reach_seq) {
  PiCorralResult result;
  if (corral_seq_ > 0xFF000000u) {
    corral_id_.fill(0);
    corral_seq_ = 0;
  }
  corral_seq_ += kMaxCells;
  int num_corrals = 0;

  int corral_cells_left = static_cast<int>(board.playable_cells().size()) -
                          region_size - board.box_count();
  if (corral_cells_left <= 0) return result;

  for (int cell : board.playable_cells()) {
    if (board.blocked(cell) || reach[cell] == reach_seq) continue;
    if (corral_id_[cell] >= corral_seq_) continue;

    ++num_corrals;
    const uint32_t cid = corral_seq_ + num_corrals;
    int head = 0, tail = 0;
    bfs_q_[tail++] = cell;
    corral_id_[cell] = cid;

    bool has_goal = false;
    int db_count = 0;
    std::array<int, kMaxBoxes> db{};

    while (head < tail) {
      const int curr = bfs_q_[head++];
      if (board.is_goal(curr)) has_goal = true;

      for (int d = 0; d < 4; ++d) {
        const int next = curr + board.dir_offset(d);
        if (board.board(next) == '#') continue;

        if (board.is_box(next)) {
          bool found = false;
          for (int b = 0; b < db_count; ++b) {
            if (db[b] == next) {
              found = true;
              break;
            }
          }
          if (!found && db_count < board.box_count()) db[db_count++] = next;
        } else if (corral_id_[next] != cid) {
          if (reach[next] != reach_seq) {
            corral_id_[next] = cid;
            bfs_q_[tail++] = next;
          }
        }
      }
    }

    corral_cells_left -= tail;

    bool needs_work = has_goal;
    if (!needs_work) {
      for (int b = 0; b < db_count; ++b) {
        if (!board.is_goal(db[b])) {
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
    bool any_valid_inward = false;

    for (int b = 0; b < db_count && is_pi; ++b) {
      const int b_pos = db[b];
      for (int d = 0; d < 4; ++d) {
        const int to = b_pos + board.dir_offset(d);
        const int from = b_pos - board.dir_offset(d);
        const bool to_is_empty = !board.blocked(to);
        const bool from_reach = (reach[from] == reach_seq);

        if (to_is_empty && from_reach) {
          if (corral_id_[to] != cid) {
            is_pi = false;
            break;
          } else {
            any_valid_inward = true;
          }
        }

        if (to_is_empty && corral_id_[to] == cid) {
          if (!from_reach && board.board(from) != '#') {
            if (board.is_box(from)) {
              bool from_in_db = false;
              for (int k = 0; k < 4; ++k) {
                if (corral_id_[from + board.dir_offset(k)] == cid) {
                  from_in_db = true;
                  break;
                }
              }
              if (!from_in_db) {
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
      if (!any_valid_inward) {
        result.is_deadlock = true;
        return result;
      }
      result.corral_id = cid;
      return result;
    }

    if (corral_cells_left <= 0) break;
  }

  return result;
}

bool DeadlockDetector::TryAugmentMatch(
    const Board& board, const std::array<int, kMaxBoxes>& boxes,
    const std::array<int, kMaxBoxes>& unmatched_b,
    const std::array<int, kMaxBoxes>& unmatched_g, int ug_count, int box_idx,
    std::array<int, kMaxBoxes>& goal_match,
    std::array<bool, kMaxBoxes>& goal_visited) const {
  const int box_pos = boxes[unmatched_b[box_idx]];
  for (int gi = 0; gi < ug_count; ++gi) {
    if (goal_visited[gi]) continue;
    if (board.goal_dist(box_pos, unmatched_g[gi]) >= kUnreachableDistance) {
      continue;
    }
    goal_visited[gi] = true;
    if (goal_match[gi] == -1 ||
        TryAugmentMatch(board, boxes, unmatched_b, unmatched_g, ug_count,
                        goal_match[gi], goal_match, goal_visited)) {
      goal_match[gi] = box_idx;
      return true;
    }
  }
  return false;
}

bool DeadlockDetector::HasFeasibleGoalAssignment(
    const Board& board, const std::array<int, kMaxBoxes>& boxes,
    const std::array<int, kMaxBoxes>& cached_goal_of) const {
  bool fully_assigned = true;
  for (int b = 0; b < board.box_count(); ++b) {
    if (cached_goal_of[b] == -1) {
      fully_assigned = false;
      break;
    }
  }
  if (fully_assigned) return true;

  uint32_t used_g = 0;
  uint32_t used_b = 0;
  for (int b = 0; b < board.box_count(); ++b) {
    const int g = board.goal_index_map(boxes[b]);
    if (g != -1) {
      used_g |= 1u << g;
      used_b |= 1u << b;
    }
  }

  std::array<int, kMaxBoxes> unmatched_b{};
  std::array<int, kMaxBoxes> unmatched_g{};
  int ub_count = 0, ug_count = 0;

  for (int b = 0; b < board.box_count(); ++b) {
    if (!(used_b & (1u << b))) unmatched_b[ub_count++] = b;
  }
  for (int g = 0; g < board.goal_count(); ++g) {
    if (!(used_g & (1u << g))) unmatched_g[ug_count++] = g;
  }

  std::array<int, kMaxBoxes> goal_match;
  goal_match.fill(-1);

  for (int bi = 0; bi < ub_count; ++bi) {
    std::array<bool, kMaxBoxes> goal_visited{};
    goal_visited.fill(false);
    if (!TryAugmentMatch(board, boxes, unmatched_b, unmatched_g, ug_count, bi,
                         goal_match, goal_visited)) {
      return false;
    }
  }
  return true;
}
