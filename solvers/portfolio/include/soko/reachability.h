// Box layout and player reachability for the state being expanded.
#ifndef NATIVE_INCLUDE_SOKO_REACHABILITY_H_
#define NATIVE_INCLUDE_SOKO_REACHABILITY_H_

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "soko/board.h"
#include "soko/types.h"

namespace soko {

// Lookup table indexed by the eight-cell ring around a cell. An entry is true when
// removing the centre cell keeps the free neighbours connected.
constexpr std::array<bool, 256> MakeCutVertexTable() {
  std::array<bool, 256> table{};
  for (int mask = 0; mask < 256; ++mask) {
    int ortho_count = 0;
    for (int i = 0; i < 8; i += 2) {
      if (mask & (1 << i)) ++ortho_count;
    }
    if (ortho_count <= 1) {
      table[mask] = true;
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
    table[mask] = ok;
  }
  return table;
}
inline constexpr std::array<bool, 256> kCutVertexOk = MakeCutVertexTable();

// Tracks which cells hold boxes and which cells the player can reach.
// One flood-fill template serves three jobs: the region of the player,
// the normalized player after a push, and the shortest walk path.
class Reachability {
 public:
  explicit Reachability(const Board& board);

  // Box layout. Bit 0 = blocked (wall or box), bit 1 = box.
  void SetBox(int cell, bool value) noexcept {
    cell_flags_[cell] = value ? 3 : 0;
  }
  void ResetBoxes() noexcept;
  bool IsBox(u16 cell) const noexcept { return (cell_flags_[cell] & 2) != 0; }
  bool Blocked(u16 cell) const noexcept { return (cell_flags_[cell] & 1) != 0; }
  void SetBlocked(u16 cell, bool value) noexcept {
    if (value) {
      cell_flags_[cell] |= 1;
    } else {
      cell_flags_[cell] &= static_cast<u8>(~1u);
    }
  }

  // Player region of the last FloodRegion call.
  bool Reaches(u16 cell) const noexcept { return reach_[cell] == reach_seq_; }
  u32 ReachMark(u16 cell) const noexcept { return reach_[cell]; }
  void SetReachMark(u16 cell, u32 mark) noexcept { reach_[cell] = mark; }
  u32 reach_seq() const noexcept { return reach_seq_; }
  u16 second_norm() const noexcept { return region_second_norm_; }
  int region_size() const noexcept { return region_size_; }
  int WalkDist(u16 cell) const noexcept { return walk_dist_[cell]; }

  // Floods the region of the player and returns the smallest cell in it.
  // The template flag chooses whether walking distances are stored.
  template <bool kWalkDist>
  u16 FloodRegion(u16 start) noexcept {
    if (reach_seq_ > 0xFF000000u) {
      std::fill(reach_.begin(), reach_.end(), 0);
      reach_seq_ = 0;
    }
    ++reach_seq_;
    return Flood<kWalkDist ? kFloodWalkDist | kFloodRegion : kFloodRegion>(
        start, reach_.data(), reach_seq_, kNoTarget);
  }

  // Returns the smallest cell reachable from `start` without touching the stored region.
  u16 NormalizeAfterPush(u16 start) noexcept {
    if (visited_seq_ > 0xFF000000u) {
      std::fill(visited_.begin(), visited_.end(), 0);
      visited_seq_ = 0;
    }
    ++visited_seq_;
    return Flood<0>(start, visited_.data(), visited_seq_, kNoTarget);
  }

  // Appends the moves of a shortest walk from `from` to `target`. Returns false
  // when the target is unreachable.
  bool AppendWalk(u16 from, u16 target, std::string* moves);

  // True when the free cells around `dest` stay connected without it. The cell
  // `origin` counts as reachable during the test.
  bool DestNotCutVertex(u16 origin, u16 dest) noexcept {
    const u32 previous = reach_[origin];
    reach_[origin] = reach_seq_;
    unsigned mask = 0;
    for (int i = 0; i < 8; ++i) {
      const bool in = (reach_[static_cast<u16>(dest + board_.ring[i])] == reach_seq_);
      mask |= static_cast<unsigned>(in) << i;
    }
    reach_[origin] = previous;
    return kCutVertexOk[mask];
  }

  // Scratch queue shared by modules that run one at a time.
  u16* queue() noexcept { return queue_.data(); }

 private:
  static constexpr unsigned kFloodWalkDist = 1;
  static constexpr unsigned kFloodRegion = 2;
  static constexpr unsigned kFloodParents = 4;
  static constexpr u16 kNoTarget = 0xFFFF;

  template <unsigned kFlags>
  u16 Flood(u16 start, u32* marks, u32 seq, u16 target) noexcept {
    int head = 0, tail = 0;
    u16* q = queue_.data();
    const u8* flags = cell_flags_.data();
    q[tail++] = start;
    marks[start] = seq;
    if constexpr (kFlags & kFloodWalkDist) walk_dist_[start] = 0;
    u16 norm = kUnreach;
    u16 second = kUnreach;

    while (head < tail) {
      const u16 c = q[head++];
      if constexpr (kFlags & kFloodParents) {
        if (c == target) break;
      }
      if constexpr (kFlags & kFloodRegion) {
        if (c < norm) {
          second = norm;
          norm = c;
        } else if (c < second) {
          second = c;
        }
      } else {
        if (c < norm) norm = c;
      }
      for (int d = 0; d < 4; ++d) {
        const u16 n = static_cast<u16>(c + board_.dirs[d]);
        if (!(flags[n] & 1) && marks[n] != seq) {
          marks[n] = seq;
          if constexpr (kFlags & kFloodWalkDist) walk_dist_[n] = walk_dist_[c] + 1;
          if constexpr (kFlags & kFloodParents) {
            parent_[n] = c;
            move_dir_[n] = d;
          }
          q[tail++] = n;
        }
      }
    }
    if constexpr (kFlags & kFloodRegion) {
      region_second_norm_ = second;
      region_size_ = tail;
    }
    return norm;
  }

  const Board& board_;
  std::vector<u8> cell_flags_;
  std::vector<u32> visited_;
  std::vector<u32> reach_;
  std::vector<int> walk_dist_;
  std::vector<u16> queue_;
  std::vector<int> parent_;
  std::vector<int> move_dir_;
  u32 visited_seq_ = 0;
  u32 reach_seq_ = 0;
  u16 region_second_norm_ = kUnreach;
  int region_size_ = 0;
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_REACHABILITY_H_
