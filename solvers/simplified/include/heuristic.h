#ifndef HEURISTIC_H_
#define HEURISTIC_H_

#include <array>
#include "board.h"

constexpr int kFullRefreshInterval = 8;

int HungarianMinCost(int num_boxes, int num_goals, const CostMatrix& cost, 
                     std::array<int, kMaxBoxes>& out_match);

int CalculateLinearConflict(const Board& board, const std::array<int, kMaxBoxes>& boxes,
                            const std::array<int, kMaxBoxes>& goal_of);

int CalculateHeuristic(const Board& board, const std::array<int, kMaxBoxes>& boxes,
                       std::array<int, kMaxBoxes>& out_goal_of);

int CalculateHeuristicIncremental(const Board& board, const std::array<int, kMaxBoxes>& boxes,
                                  std::array<int, kMaxBoxes>& goal_of,
                                  int moved_idx, int depth);

#endif  // HEURISTIC_H_
