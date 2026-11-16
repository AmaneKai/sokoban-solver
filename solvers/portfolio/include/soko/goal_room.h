// Goal room: a dead-end region holding goals behind one gate cell.
#ifndef NATIVE_INCLUDE_SOKO_GOAL_ROOM_H_
#define NATIVE_INCLUDE_SOKO_GOAL_ROOM_H_

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "flags.h"
#include "soko/board.h"
#include "soko/reachability.h"
#include "soko/types.h"

namespace soko {

// A box enters the room only through gate -> entry. It then jumps to the next goal
// of a precomputed packing order. Boxes inside the room never move again.
class GoalRoom {
 public:
  GoalRoom(const Board& board, const ThreadConfig& config);

  bool ok() const noexcept { return ok_; }
  bool InRoom(u16 cell) const noexcept { return in_room_[cell]; }
  u16 gate() const noexcept { return gate_; }
  u16 entry() const noexcept { return entry_; }
  int goal_count() const noexcept { return goal_count_; }
  int filled() const noexcept { return filled_; }
  u16 order(int k) const noexcept { return order_[k]; }
  u16 push_len(int k) const noexcept { return push_len_[k]; }
  bool heuristic_enabled() const noexcept { return heuristic_enabled_; }

  // Counts the boxes already inside the room.
  void CountFilled(const u16* boxes) noexcept {
    filled_ = 0;
    for (int i = 0; i < board_.box_count; ++i) filled_ += in_room_[boxes[i]] ? 1 : 0;
  }

  // Estimate when the room holds every goal: push distance to the entry plus the
  // in-room pushes still to come.
  int RoomHeuristic(const u16* boxes, u8* out_goal_of) const;

  // Searches (box cell, player side) states for a push sequence that brings `box` to
  // the entry while the other boxes stay put. `reach` must hold the player region.
  bool DeliveryPath(Reachability& reach, u16 box, std::vector<std::pair<u16, u8>>* pushes,
                    int* push_count, u8* last_dir);

  // Replays the in-room part of a placement: box from the entry to `goal`, player from the gate.
  bool ReplayPlacement(const Reachability& reach, u16 player, u16 goal, std::string* moves,
                       u16* final_player) const;

 private:
  void Build();
  bool RoomPath(int box, int player, int goal, const std::vector<char>& occupied,
                std::string* moves, int* pushes, int* final_player) const;
  bool PackRoom(u32 filled, int placed, std::vector<u32>& failed);
  void BuildEntryDistances();

  const Board& board_;
  const ThreadConfig& config_;

  bool ok_ = false;
  std::array<bool, kMaxCells> in_room_{};
  u16 gate_ = 0;
  u16 entry_ = 0;
  int goal_count_ = 0;
  int filled_ = 0;
  std::array<u16, kMaxBoxes> order_{};
  std::array<u16, kMaxBoxes> push_len_{};
  std::vector<u16> cells_;
  std::vector<std::array<int, 4>> nbr_;
  std::vector<int> local_;
  std::vector<u16> entry_dist_;
  bool heuristic_enabled_ = false;

  std::vector<u32> dl_seen_;
  std::vector<int> dl_parent_;
  std::vector<u8> dl_dir_;
  std::vector<u32> dl_flood_;
  std::vector<int> dl_queue_;
  std::vector<u16> dl_stack_;
  u32 dl_seq_ = 0;
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_GOAL_ROOM_H_
