#include "heuristic.h"
#include <cmath>

static int GreedyMatchCost(int num_boxes, int num_goals, const CostMatrix& cost,
                           std::array<int, kMaxBoxes>& out_match) {
  if (num_boxes > num_goals) return -1;
  out_match.fill(-1);
  uint32_t used = 0;
  int total = 0;
  for (int i = 0; i < num_boxes; ++i) {
    int best = -1;
    int best_d = kUnreachableDistance;
    for (int j = 0; j < num_goals; ++j) {
      if (used & (1u << j)) continue;
      if (cost[i][j] < best_d) {
        best_d = cost[i][j];
        best = j;
      }
    }
    if (best == -1) return -1;
    used |= 1u << best;
    out_match[i] = best;
    total += best_d;
  }
  return total;
}

int HungarianMinCost(int num_boxes, int num_goals, const CostMatrix& cost,
                     std::array<int, kMaxBoxes>& out_match) {
  if (num_boxes > num_goals) return -1;
  if (num_boxes == 0) return 0;

  std::array<int, kMaxBoxes + 1> u{};
  std::array<int, kMaxBoxes + 1> v{};
  std::array<int, kMaxBoxes + 1> p{};
  std::array<int, kMaxBoxes + 1> way{};

  for (int i = 1; i <= num_boxes; ++i) {
    p[0] = i;
    int j0 = 0;
    std::array<int, kMaxBoxes + 1> minv;
    minv.fill(kUnreachableDistance);
    std::array<bool, kMaxBoxes + 1> used;
    used.fill(false);

    do {
      used[j0] = true;
      int i0 = p[j0];
      int delta = kUnreachableDistance;
      int j1 = 0;

      for (int j = 1; j <= num_goals; ++j) {
        if (!used[j]) {
          int cur = cost[i0 - 1][j - 1] - u[i0] - v[j];
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

      for (int j = 0; j <= num_goals; ++j) {
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
      int j1 = way[j0];
      p[j0] = p[j1];
      j0 = j1;
    } while (j0 != 0);
  }

  int total_cost = 0;
  out_match.fill(-1);
  for (int j = 1; j <= num_goals; ++j) {
    if (p[j] > 0) {
      int box_idx = p[j] - 1;
      int goal_idx = j - 1;
      if (cost[box_idx][goal_idx] >= kUnreachableDistance) return -1;
      total_cost += cost[box_idx][goal_idx];
      out_match[box_idx] = goal_idx;
    }
  }

  return total_cost;
}

int CalculateLinearConflict(const Board& board,
                            const std::array<int, kMaxBoxes>& boxes,
                            const std::array<int, kMaxBoxes>& goal_of) {
  int penalty = 0;
  const int w = board.width();

  for (int i = 0; i < board.box_count() - 1; ++i) {
    if (goal_of[i] == -1) continue;
    const int ri = boxes[i] / w;
    const int ci = boxes[i] % w;
    const int gri = board.goals()[goal_of[i]] / w;
    const int gci = board.goals()[goal_of[i]] % w;

    for (int j = i + 1; j < board.box_count(); ++j) {
      if (goal_of[j] == -1) continue;
      const int rj = boxes[j] / w;
      const int cj = boxes[j] % w;
      const int grj = board.goals()[goal_of[j]] / w;
      const int gcj = board.goals()[goal_of[j]] % w;

      if (ri == rj && gri == ri && grj == rj) {
        if (std::abs(ci - cj) == 1 && ((ci < cj) != (gci < gcj))) penalty += 2;
      }
      if (ci == cj && gci == ci && gcj == cj) {
        if (std::abs(ri - rj) == 1 && ((ri < rj) != (gri < grj))) penalty += 2;
      }
    }
  }

  return penalty;
}

int CalculateHeuristic(const Board& board,
                       const std::array<int, kMaxBoxes>& boxes,
                       std::array<int, kMaxBoxes>& out_goal_of) {
  out_goal_of.fill(-1);

  uint32_t used_g = 0;
  uint32_t used_b = 0;

  for (int b = 0; b < board.box_count(); ++b) {
    const int g = board.goal_index_map(boxes[b]);
    if (g != -1) {
      used_g |= 1u << g;
      used_b |= 1u << b;
      out_goal_of[b] = g;
    }
  }

  std::array<int, kMaxBoxes> unmatched_b{};
  std::array<int, kMaxBoxes> unmatched_g{};
  int ub_count = 0;
  int ug_count = 0;

  for (int b = 0; b < board.box_count(); ++b) {
    if (!(used_b & (1u << b))) unmatched_b[ub_count++] = b;
  }
  for (int g = 0; g < board.goal_count(); ++g) {
    if (!(used_g & (1u << g))) unmatched_g[ug_count++] = g;
  }

  if (ub_count == 0) {
    return CalculateLinearConflict(board, boxes, out_goal_of);
  }

  CostMatrix cost_matrix{};
  for (int i = 0; i < ub_count; ++i) {
    const int b_pos = boxes[unmatched_b[i]];
    for (int j = 0; j < ug_count; ++j) {
      cost_matrix[i][j] = board.heuristic_dist(b_pos, unmatched_g[j]);
    }
  }

  std::array<int, kMaxBoxes> match_res;
  int match_cost =
      kUseHungarian
          ? HungarianMinCost(ub_count, ug_count, cost_matrix, match_res)
          : GreedyMatchCost(ub_count, ug_count, cost_matrix, match_res);
  if (match_cost < 0) return kUnreachableDistance;

  for (int i = 0; i < ub_count; ++i) {
    out_goal_of[unmatched_b[i]] = unmatched_g[match_res[i]];
  }

  return match_cost + CalculateLinearConflict(board, boxes, out_goal_of);
}

int CalculateHeuristicIncremental(const Board& board,
                                  const std::array<int, kMaxBoxes>& boxes,
                                  std::array<int, kMaxBoxes>& goal_of,
                                  int moved_idx, int depth) {
  if (depth % kFullRefreshInterval == 0) {
    return CalculateHeuristic(board, boxes, goal_of);
  }

  const int moved_pos = boxes[moved_idx];
  uint32_t used_g = 0;
  for (int i = 0; i < board.box_count(); ++i) {
    if (i != moved_idx && goal_of[i] != -1) {
      used_g |= 1u << goal_of[i];
    }
  }

  int best_g = -1;
  int best_d = kUnreachableDistance;
  for (int g = 0; g < board.goal_count(); ++g) {
    if (used_g & (1u << g)) continue;
    const int d = board.heuristic_dist(moved_pos, g);
    if (d < best_d) {
      best_d = d;
      best_g = g;
    }
  }

  if (best_g == -1) {
    return CalculateHeuristic(board, boxes, goal_of);
  }

  goal_of[moved_idx] = best_g;

  for (int i = 0; i < board.box_count(); ++i) {
    if (i == moved_idx || goal_of[i] == -1) continue;
    const int cur_cost = board.heuristic_dist(moved_pos, goal_of[moved_idx]) +
                         board.heuristic_dist(boxes[i], goal_of[i]);
    const int swap_cost = board.heuristic_dist(moved_pos, goal_of[i]) +
                          board.heuristic_dist(boxes[i], goal_of[moved_idx]);
    if (swap_cost < cur_cost) {
      std::swap(goal_of[moved_idx], goal_of[i]);
    }
  }

  int total_h = 0;
  bool all_solved = true;
  for (int i = 0; i < board.box_count(); ++i) {
    if (!board.is_goal(boxes[i])) all_solved = false;
    if (goal_of[i] != -1) {
      total_h += board.heuristic_dist(boxes[i], goal_of[i]);
    } else {
      total_h += board.min_goal_dist(boxes[i]) + 1000;
    }
  }

  total_h += CalculateLinearConflict(board, boxes, goal_of);
  return all_solved ? 0 : total_h;
}
