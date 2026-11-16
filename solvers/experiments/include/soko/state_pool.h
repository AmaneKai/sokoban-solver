// Memory for search states and the transposition table that detects repeated states.
#ifndef NATIVE_INCLUDE_SOKO_STATE_POOL_H_
#define NATIVE_INCLUDE_SOKO_STATE_POOL_H_

#include <cstring>

#include "soko/types.h"

namespace soko {

// Fixed-size arena of SokoState records. Index 0 means "none". Index 1 is the root.
class StatePool {
 public:
  explicit StatePool(int box_count)
      : box_count_(box_count),
        stride_(static_cast<u32>(
            (sizeof(SokoState) + box_count * (sizeof(u16) + sizeof(u8)) + 7) &
            ~static_cast<size_t>(7))),
        buffer_(AllocAlignedUninitialized(static_cast<size_t>(stride_) * kStatePoolSize)),
        base_(buffer_.get()) {}

  SokoState& At(u32 idx) noexcept {
    return *reinterpret_cast<SokoState*>(base_ + static_cast<size_t>(idx) * stride_);
  }
  const SokoState& At(u32 idx) const noexcept {
    return *reinterpret_cast<const SokoState*>(base_ + static_cast<size_t>(idx) * stride_);
  }

  // Goal index of each box, stored after the box array.
  u8* GoalOf(SokoState& s) const noexcept { return reinterpret_cast<u8*>(s.boxes + box_count_); }
  const u8* GoalOf(const SokoState& s) const noexcept {
    return reinterpret_cast<const u8*>(s.boxes + box_count_);
  }

  u32 Allocate() noexcept { return next_++; }
  void ReleaseLast() noexcept { --next_; }
  u32 next_index() const noexcept { return next_; }
  bool Full() const noexcept { return next_ >= static_cast<u32>(kStatePoolSize - 1); }

  void Prefetch(u32 idx) const noexcept {
    __builtin_prefetch(base_ + static_cast<size_t>(idx) * stride_, 0, 1);
  }

 private:
  int box_count_;
  u32 stride_;
  AlignedByteBuffer buffer_;
  u8* base_;
  u32 next_ = 1;
};

// Open-addressing hash set. Each slot packs an 8-bit hash tag and a 24-bit pool index.
class TranspositionTable {
 public:
  TranspositionTable(const StatePool& pool, int box_count)
      : pool_(pool), box_count_(box_count), slots_(AllocZeroed(kHashSize)) {}

  // Returns false when an equal state is already stored.
  bool Insert(u32 pool_idx, const SokoState& cand) noexcept {
    const u64 hash = cand.hash;
    u32 slot = static_cast<u32>(hash) & kHashMask;
    const u32 tag = static_cast<u32>(hash >> 56);

    while (slots_[slot] != 0) {
      const u32 entry = slots_[slot];
      if ((entry >> 24) == tag) {
        const u32 existing = entry & 0xFFFFFFu;
        const SokoState& ex = pool_.At(existing);
        if (ex.hash == hash && ex.normalized_player == cand.normalized_player &&
            std::memcmp(ex.boxes, cand.boxes, box_count_ * sizeof(u16)) == 0)
          return false;
      }
      slot = (slot + 1) & kHashMask;
    }
    slots_[slot] = (tag << 24) | pool_idx;
    return true;
  }

  void Prefetch(u64 hash) const noexcept {
    __builtin_prefetch(&slots_[static_cast<u32>(hash) & kHashMask], 1, 1);
  }

 private:
  const StatePool& pool_;
  int box_count_;
  ZeroedU32Array slots_;
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_STATE_POOL_H_
