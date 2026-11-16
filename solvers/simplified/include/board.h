#ifndef BOARD_H_
#define BOARD_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "flags.h"

constexpr int kMaxCells = 17424;
constexpr int kMaxBoxes = 32;
constexpr int kUnreachableDistance = 100'000;

enum Direction {
  kUp = 0,
  kDown = 1,
  kLeft = 2,
  kRight = 3,
};

const std::array<char, 4> kMoveChars = {'u', 'd', 'l', 'r'};

using CostMatrix = std::array<std::array<int, kMaxBoxes>, kMaxBoxes>;

class Board {
 public:
  void Initialize(int width, int height, char** map, char** items);
  void PrecomputePullDistances();
  void BuildFreezeStaticTable();

  int width() const { return width_; }
  int height() const { return height_; }
  int area() const { return area_; }
  int box_count() const { return box_count_; }
  int goal_count() const { return goal_count_; }
  int initial_player() const { return initial_player_; }

  const std::array<int, kMaxBoxes>& initial_boxes() const {
    return initial_boxes_;
  }
  const std::array<int, kMaxBoxes>& goals() const { return goals_; }
  const std::vector<int>& playable_cells() const { return playable_cells_; }

  int dir_offset(int dir) const { return dir_offset_[dir]; }
  int ring_offset(int idx) const { return ring_[idx]; }

  char board(int cell) const { return board_[cell]; }
  bool is_goal(int cell) const { return is_goal_[cell]; }
  int goal_index_map(int cell) const { return goal_index_map_[cell]; }
  int goal_dist(int cell, int goal) const { return goal_dist_[cell][goal]; }
  int min_goal_dist(int cell) const { return min_goal_dist_[cell]; }

  int heuristic_dist(int cell, int goal) const {
    if (!kUseManhattan) 
      return goal_dist_[cell][goal];

    const int g = goals_[goal];

    return std::abs(cell / width_ - g / width_) + std::abs(cell % width_ - g % width_);
  }

  uint8_t frz_static(int cell) const { return frz_static_[cell]; }

  bool blocked(int cell) const { return blocked_[cell]; }
  void set_blocked(int cell, bool val) { blocked_[cell] = val; }

  bool is_box(int cell) const { return is_box_[cell]; }
  void set_box(int cell, bool val) { is_box_[cell] = val; }

 private:
  int width_ = 0;
  int height_ = 0;
  int area_ = 0;
  int box_count_ = 0;
  int goal_count_ = 0;
  int initial_player_ = 0;

  std::array<int, 4> dir_offset_{};
  std::array<int, 8> ring_{};
  std::array<int, kMaxBoxes> initial_boxes_{};
  std::array<int, kMaxBoxes> goals_{};
  std::vector<int> playable_cells_;

  std::array<char, kMaxCells> board_{};
  std::array<bool, kMaxCells> is_goal_{};
  std::array<int, kMaxCells> goal_index_map_{};
  std::array<bool, kMaxCells> is_box_{};
  std::array<bool, kMaxCells> blocked_{};
  std::array<std::array<int, kMaxBoxes>, kMaxCells> goal_dist_{};
  std::array<int, kMaxCells> min_goal_dist_{};
  std::array<uint8_t, kMaxCells> frz_static_{};
};

#endif  // BOARD_H_
