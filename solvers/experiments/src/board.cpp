#include "soko/board.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace soko {

Board::Board(int width, int height, char** map_rows, char** item_rows, bool build_tunnels) {
  BuildGrid(width, height, map_rows, item_rows);
  BuildLineSegments(height);
  BuildZobristTables();
  PrecomputePullDistances();
  PrecomputeGoalDepth();
  if (build_tunnels) PrecomputeTunnels();
}

void Board::BuildGrid(int width, int height, char** map_rows, char** item_rows) {
  stride = width + 4;
  area = stride * (height + 4);
  box_count = 0;
  goal_count = 0;

  std::fill_n(is_goal.data(), area, false);
  std::fill_n(goal_idx.data(), area, kNoGoal);

  dirs[0] = -stride;
  dirs[1] = stride;
  dirs[2] = -1;
  dirs[3] = 1;

  ring[0] = -stride;
  ring[1] = -stride + 1;
  ring[2] = 1;
  ring[3] = stride + 1;
  ring[4] = stride;
  ring[5] = stride - 1;
  ring[6] = -1;
  ring[7] = -stride - 1;

  for (int i = 0; i < area; ++i) {
    map[i] = '#';
    row_of[i] = static_cast<u8>(i / stride);
    col_of[i] = static_cast<u8>(i % stride);
  }

  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      const int idx = (r + 2) * stride + (c + 2);
      const char m = map_rows[r][c];
      const char it = item_rows[r][c];

      map[idx] = m;

      if (m == '.' || m == '*' || it == '*' || it == '.') {
        is_goal[idx] = true;
        map[idx] = ' ';
        if (goal_count < kMaxBoxes) {
          goal_idx[idx] = static_cast<u8>(goal_count);
          goals[goal_count++] = static_cast<u16>(idx);
        }
      }

      if (it == '@' || it == '+') initial_player = static_cast<u16>(idx);

      if (it == '$' || it == '*') {
        if (box_count < kMaxBoxes) initial_boxes[box_count++] = static_cast<u16>(idx);
      }
    }
  }

  playable_count = 0;
  for (int i = 0; i < area; ++i) {
    if (!IsWall(i)) playable_cells[playable_count++] = static_cast<u16>(i);
  }
}

// Gives each wall-free run of a row or column one segment id. Two cells share a
// segment when no wall lies between them on that line.
void Board::BuildLineSegments(int height) {
  u16 seg_id = 1;
  for (int r = 0; r < height + 4; ++r) {
    ++seg_id;
    for (int c = 0; c < stride; ++c) {
      const int idx = r * stride + c;
      if (IsWall(idx)) {
        row_seg[idx] = 0;
        ++seg_id;
      } else {
        row_seg[idx] = seg_id;
      }
    }
  }
  for (int c = 0; c < stride; ++c) {
    ++seg_id;
    for (int r = 0; r < height + 4; ++r) {
      const int idx = r * stride + c;
      if (IsWall(idx)) {
        col_seg[idx] = 0;
        ++seg_id;
      } else {
        col_seg[idx] = seg_id;
      }
    }
  }
}

void Board::BuildZobristTables() {
  u32 z_state = 123456789u;
  for (int i = 0; i < area; ++i) {
    zobrist_box[i] = (static_cast<u64>(Xorshift32(&z_state)) << 32) | Xorshift32(&z_state);
    zobrist_player[i] = (static_cast<u64>(Xorshift32(&z_state)) << 32) | Xorshift32(&z_state);
  }
}

// Runs one reverse breadth-first search per goal. Each search pulls a box away from
// the goal, so goal_dist[cell][goal] is the push count from the cell to the goal.
void Board::PrecomputePullDistances() {
  std::memset(&goal_dist[0], 0xFF, area * sizeof(goal_dist[0]));
  std::vector<u16> queue(kMaxCells);

  for (int g = 0; g < goal_count; ++g) {
    int head = 0, tail = 0;
    goal_dist[goals[g]][g] = 0;
    queue[tail++] = goals[g];

    while (head < tail) {
      const u16 curr = queue[head++];
      for (int d = 0; d < 4; ++d) {
        const u16 from = static_cast<u16>(curr - dirs[d]);
        const u16 player_stand = static_cast<u16>(from - dirs[d]);
        if (!IsWall(from) && !IsWall(player_stand)) {
          if (goal_dist[from][g] > goal_dist[curr][g] + 1) {
            goal_dist[from][g] = goal_dist[curr][g] + 1;
            queue[tail++] = from;
          }
        }
      }
    }
  }

  for (int i = 0; i < area; ++i) {
    int min_d = kUnreach;
    for (int g = 0; g < goal_count; ++g) {
      if (goal_dist[i][g] < min_d) min_d = goal_dist[i][g];
    }
    min_dist[i] = min_d;
  }
}

// Measures how deep each goal sits inside its goal cluster. The measure is a
// breadth-first distance from non-goal floor plus the adjacent wall count.
// Deeper goals are filled before goals near the entrance.
void Board::PrecomputeGoalDepth() {
  constexpr int kFar = 1'000'000;
  std::vector<u16> queue(kMaxCells);
  std::fill_n(goal_depth.data(), kMaxBoxes, 0);
  std::vector<int> cluster_depth(area, kFar);
  int head = 0, tail = 0;
  for (int pi = 0; pi < playable_count; ++pi) {
    const u16 c = playable_cells[pi];
    if (!is_goal[c]) {
      cluster_depth[c] = 0;
      queue[tail++] = c;
    }
  }
  while (head < tail) {
    const u16 curr = queue[head++];
    for (int d = 0; d < 4; ++d) {
      const u16 next = static_cast<u16>(curr + dirs[d]);
      if (!IsWall(next) && cluster_depth[next] > cluster_depth[curr] + 1) {
        cluster_depth[next] = cluster_depth[curr] + 1;
        queue[tail++] = next;
      }
    }
  }
  for (int g = 0; g < goal_count; ++g) {
    const u16 pos = goals[g];
    int walls = 0;
    for (int d = 0; d < 4; ++d) {
      if (IsWall(pos + dirs[d])) ++walls;
    }
    const int depth = (cluster_depth[pos] < kFar) ? cluster_depth[pos] : 1;
    goal_depth[g] = static_cast<u16>(depth * 2 + walls);
  }
}

// A tunnel is a straight corridor one cell wide. A box pushed into it can slide to
// the far end in one macro move.
void Board::PrecomputeTunnels() {
  std::array<u8, kMaxCells> open_degree{};
  for (int pi = 0; pi < playable_count; ++pi) {
    const u16 pos = playable_cells[pi];
    u8 deg = 0;
    for (int d = 0; d < 4; ++d) {
      if (!IsWall(pos + dirs[d])) ++deg;
    }
    open_degree[pos] = deg;
  }

  for (int i = 0; i < area; ++i) {
    for (int d = 0; d < 4; ++d) {
      tunnel_dest[i][d] = static_cast<u16>(i);
      tunnel_steps[i][d] = 1;
    }
  }

  for (int pi = 0; pi < playable_count; ++pi) {
    const u16 start = playable_cells[pi];
    for (int d = 0; d < 4; ++d) {
      const int perp1 = (d < 2) ? 2 : 0;
      const int perp2 = (d < 2) ? 3 : 1;

      u16 curr = start;
      u8 steps = 1;
      while (!is_goal[curr] && open_degree[curr] == 2) {
        if (!IsWall(curr + dirs[perp1]) || !IsWall(curr + dirs[perp2])) break;

        const u16 next_cell = static_cast<u16>(curr + dirs[d]);
        if (IsWall(next_cell) || min_dist[next_cell] >= kUnreach) break;

        const u16 ahead = static_cast<u16>(next_cell + dirs[d]);
        if (IsWall(ahead) && !is_goal[next_cell]) break;

        curr = next_cell;
        ++steps;
      }
      tunnel_dest[start][d] = curr;
      tunnel_steps[start][d] = steps;
    }
  }
}

}  // namespace soko
