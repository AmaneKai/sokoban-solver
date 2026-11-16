#include "board.h"

void Board::Initialize(int width, int height, char** map, char** items) {
  width_ = width + 4;
  height_ = height + 4;
  area_ = width_ * height_;

  dir_offset_[kUp] = -width_;
  dir_offset_[kDown] = width_;
  dir_offset_[kLeft] = -1;
  dir_offset_[kRight] = 1;

  ring_[0] = -width_;
  ring_[1] = -width_ + 1;
  ring_[2] = 1;
  ring_[3] = width_ + 1;
  ring_[4] = width_;
  ring_[5] = width_ - 1;
  ring_[6] = -1;
  ring_[7] = -width_ - 1;

  board_.fill('#');
  is_goal_.fill(false);
  is_box_.fill(false);
  blocked_.fill(true);
  goal_index_map_.fill(-1);

  box_count_ = 0;
  goal_count_ = 0;
  playable_cells_.clear();

  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      const int idx = (r + 2) * width_ + (c + 2);
      const char tile = map[r][c];
      const char item = items[r][c];

      board_[idx] = tile;

      if (tile == '.' || tile == '*' || item == '*' || item == '.') {
        is_goal_[idx] = true;
        board_[idx] = ' ';
        if (goal_count_ < kMaxBoxes) {
          goal_index_map_[idx] = goal_count_;
          goals_[goal_count_++] = idx;
        }
      }

      if (item == '@' || item == '+') {
        initial_player_ = idx;
      }

      if (item == '$' || item == '*') {
        if (box_count_ < kMaxBoxes) {
          initial_boxes_[box_count_++] = idx;
        }
      }
    }
  }

  for (int i = 0; i < area_; ++i) {
    blocked_[i] = (board_[i] == '#');
    if (board_[i] != '#') {
      playable_cells_.push_back(i);
    }
  }

  std::sort(initial_boxes_.begin(), initial_boxes_.begin() + box_count_);
}

void Board::PrecomputePullDistances() {
  for (int i = 0; i < area_; ++i) {
    goal_dist_[i].fill(kUnreachableDistance);
  }

  std::vector<int> bfs_q(area_);

  for (int g = 0; g < goal_count_; ++g) {
    int head = 0, tail = 0;
    goal_dist_[goals_[g]][g] = 0;
    bfs_q[tail++] = goals_[g];

    while (head < tail) {
      const int curr = bfs_q[head++];
      for (int d = 0; d < 4; ++d) {
        const int from = curr - dir_offset_[d];
        const int player_stand = from - dir_offset_[d];

        if (board_[from] != '#' && board_[player_stand] != '#') {
          if (goal_dist_[from][g] > goal_dist_[curr][g] + 1) {
            goal_dist_[from][g] = goal_dist_[curr][g] + 1;
            bfs_q[tail++] = from;
          }
        }
      }
    }
  }

  for (int i = 0; i < area_; ++i) {
    int min_d = kUnreachableDistance;
    for (int g = 0; g < goal_count_; ++g) {
      min_d = std::min(min_d, goal_dist_[i][g]);
    }
    min_goal_dist_[i] = min_d;
  }
}

void Board::BuildFreezeStaticTable() {
  frz_static_.fill(0);
  for (int pos : playable_cells_) {
    uint8_t mask = 0;
    for (int axis = 0; axis < 2; ++axis) {
      const int d = (axis == 0) ? 1 : width_;
      const int a = pos - d;
      const int b = pos + d;
      const bool wall_side = (board_[a] == '#' || board_[b] == '#');
      const bool dead_both =
          (min_goal_dist_[a] >= kUnreachableDistance &&
           min_goal_dist_[b] >= kUnreachableDistance);
      if (wall_side || dead_both) mask |= static_cast<uint8_t>(1u << axis);
    }
    frz_static_[pos] = mask;
  }
}
