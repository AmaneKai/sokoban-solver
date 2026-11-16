#include "soko/heuristic.h"

#include <algorithm>
#include <array>

namespace soko {

int HeuristicEngine::HungarianMinCost(int n, int m, const int cost[kMaxBoxes][kMaxBoxes], u8* out_match) {
  if (n > m) return -1;
  if (n == 0) return 0;

  int u[kMaxBoxes + 1];
  int v[kMaxBoxes + 1];
  int p[kMaxBoxes + 1];
  int way[kMaxBoxes + 1];

  std::fill_n(u, m + 1, 0);
  std::fill_n(v, m + 1, 0);
  std::fill_n(p, m + 1, 0);
  std::fill_n(way, m + 1, 0);

  for (int i = 1; i <= n; ++i) {
    p[0] = i;
    int j0 = 0;
    int minv[kMaxBoxes + 1];
    std::fill_n(minv, m + 1, 1'000'000'000);
    bool used[kMaxBoxes + 1];
    std::fill_n(used, m + 1, false);

    do {
      used[j0] = true;
      int i0 = p[j0], delta = 1'000'000'000, j1 = 0;
      for (int j = 1; j <= m; ++j) {
        if (!used[j]) {
          const int cur = cost[i0 - 1][j - 1] - u[i0] - v[j];
          if (cur < minv[j]) {
            minv[j] = cur;
            way[j] = j0;
          }
          if (minv[j] < delta) {
            delta = minv[j];
            j1 = j;
          }
        }
      }
      for (int j = 0; j <= m; ++j) {
        if (used[j]) {
          u[p[j]] += delta;
          v[j] -= delta;
        } else {
          minv[j] -= delta;
        }
      }
      j0 = j1;
    } while (p[j0] != 0);

    do {
      const int j1 = way[j0];
      p[j0] = p[j1];
      j0 = j1;
    } while (j0 != 0);
  }

  int total_cost = 0;
  for (int j = 1; j <= m; ++j) {
    if (p[j] > 0) {
      const int box_idx = p[j] - 1;
      const int goal_idx = j - 1;
      if (cost[box_idx][goal_idx] >= kUnreach) return -1;
      total_cost += cost[box_idx][goal_idx];
      if (out_match) out_match[box_idx] = static_cast<u8>(goal_idx);
    }
  }
  return total_cost;
}

int HeuristicEngine::GreedyMinCost(int n, int m, const int cost[kMaxBoxes][kMaxBoxes], u8* out_match) {
  if (n > m) return -1;

  u32 used = 0;
  int total_cost = 0;
  for (int i = 0; i < n; ++i) {
    int best_j = -1, best_c = 1'000'000'000;
    for (int j = 0; j < m; ++j) {
      if (!(used & (1u << j)) && cost[i][j] < best_c) {
        best_c = cost[i][j];
        best_j = j;
      }
    }
    if (best_c >= kUnreach) return -1;
    used |= 1u << best_j;
    total_cost += best_c;
    if (out_match) out_match[i] = static_cast<u8>(best_j);
  }
  return total_cost;
}

int HeuristicEngine::LinearConflictPenalty(const u16* boxes, const u8* goal_of) const {
  constexpr int kPenalty = 2;

  int penalty = 0;
  for (int axis = 0; axis < 2; ++axis) {
    std::array<u8, kMaxBoxes> line, pos, goal_pos;
    int n = 0;
    for (int i = 0; i < board_.box_count; ++i) {
      if (goal_of[i] == kNoGoal) continue;
      const u16 b = boxes[i];
      const u16 g = board_.goals[goal_of[i]];
      const bool same_unwalled_line =
          (axis == 0) ? (board_.row_seg[b] == board_.row_seg[g]) : (board_.col_seg[b] == board_.col_seg[g]);
      if (!same_unwalled_line) continue;

      line[n] = (axis == 0) ? board_.row_of[b] : board_.col_of[b];
      pos[n] = (axis == 0) ? board_.col_of[b] : board_.row_of[b];
      goal_pos[n] = (axis == 0) ? board_.col_of[g] : board_.row_of[g];
      ++n;
    }
    if (n < 2) continue;

    u32 done = 0;
    for (int i = 0; i < n; ++i) {
      if (done & (1u << i)) continue;

      std::array<u8, kMaxBoxes> order_pos, order_goal;
      int m = 0;
      for (int j = i; j < n; ++j) {
        if (line[j] != line[i]) continue;
        done |= 1u << j;
        int k = m++;
        while (k > 0 && order_pos[k - 1] > pos[j]) {
          order_pos[k] = order_pos[k - 1];
          order_goal[k] = order_goal[k - 1];
          --k;
        }
        order_pos[k] = pos[j];
        order_goal[k] = goal_pos[j];
      }
      if (m < 2) continue;
      if (m == 2) {
        if (order_goal[0] >= order_goal[1]) penalty += kPenalty;
        continue;
      }

      std::array<u8, kMaxBoxes> lis;
      int best = 0;
      for (int a = 0; a < m; ++a) {
        lis[a] = 1;
        for (int b = 0; b < a; ++b) {
          if (order_goal[b] < order_goal[a] && lis[b] + 1 > lis[a]) {
            lis[a] = static_cast<u8>(lis[b] + 1);
          }
        }
        best = std::max<int>(best, lis[a]);
      }
      penalty += kPenalty * (m - best);
    }
  }
  return penalty;
}

int HeuristicEngine::Calc(const u16* boxes, u8* out_goal_of) const {
  if (room_.heuristic_enabled()) return room_.RoomHeuristic(boxes, out_goal_of);
  if (config_.heuristic == Heuristic::kNone) {
    if (out_goal_of) {
      for (int b = 0; b < board_.box_count; ++b) out_goal_of[b] = board_.goal_idx[boxes[b]];
    }
    return 0;
  }

  u32 used_g = 0;
  u32 used_b = 0;

  for (int b = 0; b < board_.box_count; ++b) {
    const u8 g = board_.goal_idx[boxes[b]];
    if (g != kNoGoal) {
      used_g |= 1u << g;
      used_b |= 1u << b;
      if (out_goal_of) out_goal_of[b] = g;
    }
  }

  std::array<u8, kMaxBoxes> unmatched_b{};
  std::array<u8, kMaxBoxes> unmatched_g{};
  int ub_count = 0;
  int ug_count = 0;

  for (int b = 0; b < board_.box_count; ++b) {
    if (!(used_b & (1u << b))) unmatched_b[ub_count++] = static_cast<u8>(b);
  }
  for (int g = 0; g < board_.goal_count; ++g) {
    if (!(used_g & (1u << g))) unmatched_g[ug_count++] = static_cast<u8>(g);
  }

  int cost_matrix[kMaxBoxes][kMaxBoxes];
  for (int i = 0; i < ub_count; ++i) {
    const u16 b_pos = boxes[unmatched_b[i]];
    for (int j = 0; j < ug_count; ++j) {
      cost_matrix[i][j] = Dist(b_pos, unmatched_g[j]);
    }
  }

  u8 match_res[kMaxBoxes];
  int match_cost =
      config_.hungarian ? HungarianMinCost(ub_count, ug_count, cost_matrix, match_res)
                     : GreedyMinCost(ub_count, ug_count, cost_matrix, match_res);
  // Greedy matching can fail where a full matching exists (two boxes wanting the same
  // last goal). Only an exact matching may declare the state dead.
  if (match_cost < 0 && !config_.hungarian && config_.greedy_exact_fallback) {
    match_cost = HungarianMinCost(ub_count, ug_count, cost_matrix, match_res);
  }

  if (match_cost < 0) {
    if (out_goal_of) {
      for (int i = 0; i < ub_count; ++i) out_goal_of[unmatched_b[i]] = kNoGoal;
    }
    return kDeadHeuristic;
  }

  int h = match_cost;

  if (out_goal_of) {
    for (int i = 0; i < ub_count; ++i) {
      out_goal_of[unmatched_b[i]] = unmatched_g[match_res[i]];
    }
    if (config_.linear_conflict) h += LinearConflictPenalty(boxes, out_goal_of);
  } else if (config_.linear_conflict) {
    std::array<u8, kMaxBoxes> lc_goal_of;
    lc_goal_of.fill(kNoGoal);
    for (int b = 0; b < board_.box_count; ++b) {
      if (used_b & (1u << b)) lc_goal_of[b] = board_.goal_idx[boxes[b]];
    }
    for (int i = 0; i < ub_count; ++i) {
      lc_goal_of[unmatched_b[i]] = unmatched_g[match_res[i]];
    }
    h += LinearConflictPenalty(boxes, lc_goal_of.data());
  }

  if (config_.goal_room_funnel) {
    for (int i = 0; i < ug_count; ++i) {
      h += board_.goal_depth[unmatched_g[i]];
    }
  }
  return h;
}

int HeuristicEngine::CalcIncremental(const u16* boxes, u8* goal_of, int moved_idx,
                                     u16 depth) const {
  if (room_.heuristic_enabled()) return room_.RoomHeuristic(boxes, goal_of);
  const u16 moved_pos = boxes[moved_idx];
  const u16 interval = config_.hungarian ? 4 : 8;
  if (config_.heuristic == Heuristic::kNone || board_.is_goal[moved_pos] || depth % interval == 0) {
    return Calc(boxes, goal_of);
  }

  u32 used_g = 0;
  for (int i = 0; i < board_.box_count; ++i) {
    if (i != moved_idx && goal_of[i] != kNoGoal) used_g |= 1u << goal_of[i];
  }

  int best_g = -1, best_d = kUnreach;
  for (int g = 0; g < board_.goal_count; ++g) {
    if (used_g & (1u << g)) continue;
    const int d = Dist(moved_pos, g);
    if (d < best_d) {
      best_d = d;
      best_g = g;
    }
  }

  if (best_g == -1) {
    return Calc(boxes, goal_of);
  }

  goal_of[moved_idx] = static_cast<u8>(best_g);
  int moved_dist = best_d;

  for (int i = 0; i < board_.box_count; ++i) {
    if (i == moved_idx || goal_of[i] == kNoGoal) continue;
    const u8 gi = goal_of[i];
    const int current = moved_dist + Dist(boxes[i], gi);
    const int swapped = Dist(moved_pos, gi) + Dist(boxes[i], goal_of[moved_idx]);
    if (swapped < current) {
      std::swap(goal_of[moved_idx], goal_of[i]);
      moved_dist = Dist(moved_pos, goal_of[moved_idx]);
    }
  }

  int h = 0;
  bool all_on_goal = true;
  u32 filled_goals = 0;
  for (int i = 0; i < board_.box_count; ++i) {
    const u8 g = board_.goal_idx[boxes[i]];
    if (g == kNoGoal) {
      all_on_goal = false;
    } else {
      filled_goals |= 1u << g;
    }
    if (goal_of[i] != kNoGoal) h += Dist(boxes[i], goal_of[i]);
    else h += board_.min_dist[boxes[i]] + kSoftUnreach;
  }
  if (config_.linear_conflict) h += LinearConflictPenalty(boxes, goal_of);
  if (all_on_goal) {
    h = 0;
  } else if (config_.goal_room_funnel) {
    for (int g = 0; g < board_.goal_count; ++g) {
      if (!(filled_goals & (1u << g))) h += board_.goal_depth[g];
    }
  }
  return h;
}

}  // namespace soko
