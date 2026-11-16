#include "soko/reachability.h"

namespace soko {

Reachability::Reachability(const Board& board) : board_(board) {
  std::fill_n(visited_.data(), board_.area, 0);
  std::fill_n(reach_.data(), board_.area, 0);
  ResetBoxes();
}

void Reachability::ResetBoxes() noexcept {
  std::fill_n(is_box_.data(), board_.area, false);
  for (int i = 0; i < board_.area; ++i) blocked_[i] = board_.IsWall(i);
}

bool Reachability::AppendWalk(u16 from, u16 target, std::string* moves) {
  static constexpr char kWalkChar[4] = {'u', 'd', 'l', 'r'};
  if (parent_.empty()) {
    parent_.assign(board_.area, 0);
    move_dir_.assign(board_.area, 0);
  }
  if (visited_seq_ > 0xFF000000u) {
    std::fill_n(visited_.data(), board_.area, 0);
    visited_seq_ = 0;
  }
  ++visited_seq_;
  Flood<kFloodParents>(from, visited_.data(), visited_seq_, target);
  if (visited_[target] != visited_seq_) return false;

  std::string reversed;
  for (u16 cell = target; cell != from; cell = static_cast<u16>(parent_[cell])) {
    reversed.push_back(kWalkChar[move_dir_[cell]]);
  }
  moves->append(reversed.rbegin(), reversed.rend());
  return true;
}

}  // namespace soko
