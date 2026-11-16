// Static board topology: walls, goals, pull distances, line segments, and tunnel macros.
#ifndef NATIVE_INCLUDE_SOKO_BOARD_H_
#define NATIVE_INCLUDE_SOKO_BOARD_H_

#include <array>

#include "soko/types.h"

namespace soko {

// The board never changes during a search. Cells are indexed on a grid padded by two
// cells on every side, so neighbour lookups never leave the arrays.
struct Board {
  Board(int width, int height, char** map_rows, char** item_rows, bool build_tunnels);

  int stride = 0;  // padded row length (width + 4)
  int area = 0;    // padded cell count
  int box_count = 0;
  int goal_count = 0;
  u16 initial_player = 0;
  std::array<u16, kMaxBoxes> initial_boxes;
  std::array<u16, kMaxBoxes> goals;
  std::array<u16, kMaxBoxes> goal_depth;

  std::array<char, kMaxCells> map;
  std::array<bool, kMaxCells> is_goal;
  std::array<u8, kMaxCells> goal_idx;
  std::array<int, kMaxCells> min_dist;
  std::array<u8, kMaxCells> row_of;
  std::array<u8, kMaxCells> col_of;
  std::array<u16, kMaxCells> row_seg;
  std::array<u16, kMaxCells> col_seg;
  std::array<int, 8> ring;
  std::array<int, 4> dirs;

  std::array<std::array<u16, kMaxBoxes>, kMaxCells> goal_dist;
  std::array<u64, kMaxCells> zobrist_box;
  std::array<u64, kMaxCells> zobrist_player;

  std::array<std::array<u16, 4>, kMaxCells> tunnel_dest;
  std::array<std::array<u8, 4>, kMaxCells> tunnel_steps;

  std::array<u16, kMaxCells> playable_cells;
  int playable_count = 0;

  bool IsWall(int cell) const noexcept { return map[cell] == '#'; }

 private:
  void BuildGrid(int width, int height, char** map_rows, char** item_rows);
  void BuildLineSegments(int height);
  void BuildZobristTables();
  void PrecomputePullDistances();
  void PrecomputeGoalDepth();
  void PrecomputeTunnels();
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_BOARD_H_
