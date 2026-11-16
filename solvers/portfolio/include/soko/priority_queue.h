// 4-ary min-heap of state indices, with the reheaping used when the search stagnates.
#ifndef NATIVE_INCLUDE_SOKO_PRIORITY_QUEUE_H_
#define NATIVE_INCLUDE_SOKO_PRIORITY_QUEUE_H_

#include <algorithm>
#include <type_traits>

#include "soko/state_pool.h"
#include "soko/types.h"

namespace soko {

struct PQNode {
  u64 priority;
  u32 idx;
};
static_assert(sizeof(PQNode) == 16 && std::is_trivially_destructible_v<PQNode>,
              "PQNode must be 16 bytes and trivially destructible");

class PriorityQueue {
 public:
  // Offset by 3 slots inside a 64-byte-aligned buffer so every 4-ary sibling group
  // nodes_[4*i+1 .. 4*i+4] sits on a single 64-byte cache line.
  PriorityQueue()
      : buffer_(AllocAlignedUninitialized(static_cast<size_t>(kStatePoolSize + 3) *
                                          sizeof(PQNode))),
        nodes_(reinterpret_cast<PQNode*>(buffer_.get()) + 3) {}

  int size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }
  bool Full() const noexcept { return size_ >= kStatePoolSize - 1; }
  const PQNode& at(int i) const noexcept { return nodes_[i]; }

  void Push(u32 idx, u64 priority) noexcept {
    if (Full()) return;

    int i = size_++;
    while (i > 0) {
      const int p = (i - 1) >> 2;
      if (nodes_[p].priority <= priority) break;
      nodes_[i] = nodes_[p];
      i = p;
    }
    nodes_[i].priority = priority;
    nodes_[i].idx = idx;
  }

  // Removes the smallest entry and returns its state index. Returns 0 when empty.
  u32 Pop(const StatePool& pool) noexcept {
    if (size_ == 0) return 0;

    const u32 ret = nodes_[0].idx;
    pool.Prefetch(ret);
    const PQNode tmp = nodes_[--size_];
    int i = 0;

    while (true) {
      const int first_child = (i << 2) + 1;
      if (first_child >= size_) break;
      const int last_child = std::min(first_child + kHeapArity, size_);

      int best_child = first_child;
      u64 best_priority = nodes_[first_child].priority;
      for (int c = first_child + 1; c < last_child; ++c) {
        if (nodes_[c].priority < best_priority) {
          best_priority = nodes_[c].priority;
          best_child = c;
        }
      }

      if (tmp.priority <= best_priority) break;
      nodes_[i] = nodes_[best_child];
      i = best_child;
    }
    nodes_[i] = tmp;
    return ret;
  }

  // Recomputes every priority with `priority_of(const PQNode&)`, then heapifies in O(n).
  template <typename PriorityFn>
  void Rebuild(PriorityFn priority_of) noexcept {
    for (int i = 0; i < size_; ++i) nodes_[i].priority = priority_of(nodes_[i]);
    if (size_ <= 1) return;
    for (int i = (size_ - 2) / kHeapArity; i >= 0; --i) SiftDown(i);
  }

  // Empties the heap and pushes every entry again with `priority_of(idx, old_priority)`.
  // Costs O(n log n).
  template <typename PriorityFn>
  void Reheap(PriorityFn priority_of) noexcept {
    const int n = size_;
    size_ = 0;
    for (int i = 0; i < n; ++i) {
      const u32 idx = nodes_[i].idx;
      const u64 old_priority = nodes_[i].priority;
      Push(idx, priority_of(idx, old_priority));
    }
  }

  // Pops up to `count` top entries into `scratch`, then pushes them back with
  // `priority_of(idx, old_priority)`. The pushed entries sink below the rest.
  template <typename PriorityFn>
  void SinkTop(PQNode* scratch, int count, const StatePool& pool, PriorityFn priority_of) noexcept {
    const int sink_count = std::min(size_, count);
    for (int i = 0; i < sink_count; ++i) {
      scratch[i] = nodes_[0];
      Pop(pool);
    }
    for (int i = 0; i < sink_count; ++i) {
      Push(scratch[i].idx, priority_of(scratch[i].idx, scratch[i].priority));
    }
  }

 private:
  void SiftDown(int i) noexcept {
    const PQNode tmp = nodes_[i];
    while (true) {
      const int first_child = (i << 2) + 1;
      if (first_child >= size_) break;
      const int last_child = std::min(first_child + kHeapArity, size_);
      int best_child = first_child;
      for (int c = first_child + 1; c < last_child; ++c) {
        if (nodes_[c].priority < nodes_[best_child].priority) best_child = c;
      }
      if (tmp.priority <= nodes_[best_child].priority) break;
      nodes_[i] = nodes_[best_child];
      i = best_child;
    }
    nodes_[i] = tmp;
  }

  AlignedByteBuffer buffer_;
  PQNode* nodes_;
  int size_ = 0;
};

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_PRIORITY_QUEUE_H_
