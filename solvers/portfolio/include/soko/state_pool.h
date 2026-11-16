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

  u32 stride() const noexcept { return stride_; }

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
//
// The table starts small and doubles when it is half full, so a search that visits a few
// thousand states stays in cache and never touches a 128 MB array. Every state in the pool
// is in the table, so a rebuild just rescans the pool in index order.
class TranspositionTable {
 public:
  static constexpr u32 kInitialSlots = 1u << 18;

  TranspositionTable(const StatePool& pool, int box_count)
      : pool_(pool),
        box_count_(box_count),
        capacity_(kInitialSlots),
        mask_(kInitialSlots - 1),
        slots_(AllocZeroed(kInitialSlots)) {}

  // Returns false when an equal state is already stored.
  bool Insert(u32 pool_idx, const SokoState& cand) noexcept {
    const u64 hash = cand.hash;
    u32 slot = static_cast<u32>(hash) & mask_;
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
      slot = (slot + 1) & mask_;
    }
    slots_[slot] = (tag << 24) | pool_idx;
    last_slot_ = slot;
    return true;
  }

  // Takes back the most recent successful Insert. The slot was empty before that insert, so no
  // other probe chain runs through it and clearing it restores the table exactly. Call this
  // before releasing the pool entry, or the slot would point at an index that gets reused.
  void UndoLastInsert() noexcept { slots_[last_slot_] = 0; }

  void Prefetch(u64 hash) const noexcept {
    __builtin_prefetch(&slots_[static_cast<u32>(hash) & mask_], 1, 1);
  }

  // Call between expansions, when every pool entry below `next_index` is in the table.
  // Returns false only when the larger array cannot be allocated.
  bool EnsureCapacity(u32 next_index) {
    if (static_cast<u64>(next_index) * 2 <= capacity_) return true;
    return Grow(next_index);
  }

  u32 capacity() const noexcept { return capacity_; }
  u32 grow_count() const noexcept { return grow_count_; }

 private:
  bool Grow(u32 next_index) {
    u32 new_capacity = capacity_;
    while (new_capacity < static_cast<u32>(kHashSize) &&
           static_cast<u64>(next_index) * 2 > new_capacity) {
      new_capacity <<= 1;
    }
    if (new_capacity == capacity_) return true;  // Already at the maximum size.

    void* raw = std::calloc(new_capacity, sizeof(u32));
    if (!raw) return false;
    ZeroedU32Array fresh(static_cast<u32*>(raw));

    const u32 new_mask = new_capacity - 1;
    for (u32 idx = 1; idx < next_index; ++idx) {
      const u64 hash = pool_.At(idx).hash;
      u32 slot = static_cast<u32>(hash) & new_mask;
      while (fresh[slot] != 0) slot = (slot + 1) & new_mask;
      fresh[slot] = (static_cast<u32>(hash >> 56) << 24) | idx;
    }

    slots_ = std::move(fresh);
    capacity_ = new_capacity;
    mask_ = new_mask;
    ++grow_count_;
    return true;
  }

  const StatePool& pool_;
  int box_count_;
  u32 capacity_;
  u32 mask_;
  u32 last_slot_ = 0;
  u32 grow_count_ = 0;
  ZeroedU32Array slots_;
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_STATE_POOL_H_
