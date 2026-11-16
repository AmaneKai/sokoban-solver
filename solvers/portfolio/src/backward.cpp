#include "soko/backward.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "soko/reachability.h"

namespace soko {
namespace {

constexpr int kInfCost = 1'000'000;
constexpr char kMoveChar[4] = {'u', 'd', 'l', 'r'};

#ifdef SOKO_TRACE
// Trace node ids are 1-based state indices; id 0 is a virtual root standing for "the solved
// layout" (the real roots differ only by the player's region). A pull is traced as the box
// move from -> to, so the explorer can replay it like a push.
constexpr u32 kVirtualRoot = 0;
#endif

// Minimum-cost perfect matching (Hungarian method with potentials), square n x n.
// Returns kInfCost or more when no finite matching exists.
int MinCostMatching(int n, const int cost[kMaxBoxes][kMaxBoxes]) {
  if (n == 0) return 0;

  int u[kMaxBoxes + 1]{};
  int v[kMaxBoxes + 1]{};
  int p[kMaxBoxes + 1]{};
  int way[kMaxBoxes + 1]{};
  int minv[kMaxBoxes + 1];
  bool used[kMaxBoxes + 1];

  for (int i = 1; i <= n; ++i) {
    p[0] = i;
    int j0 = 0;
    std::fill_n(minv, n + 1, 2 * kInfCost);
    std::fill_n(used, n + 1, false);
    do {
      used[j0] = true;
      const int i0 = p[j0];
      int delta = 2 * kInfCost, j1 = 0;
      for (int j = 1; j <= n; ++j) {
        if (used[j]) continue;
        const int cur = cost[i0 - 1][j - 1] - u[i0] - v[j];
        if (cur < minv[j]) {
          minv[j] = cur;
          way[j] = j0;
        }
        if (minv[j] < delta) {
          delta = minv[j];
          j1 = j;
        }
      }
      for (int j = 0; j <= n; ++j) {
        if (used[j]) {
          u[p[j]] += delta;
          v[j] -= delta;
        } else {
          minv[j] -= delta;
        }
      }
      j0 = j1;
    } while (p[j0] != 0);
    do {
      const int j1 = way[j0];
      p[j0] = p[j1];
      j0 = j1;
    } while (j0);
  }

  int total = 0;
  for (int j = 1; j <= n; ++j) {
    const int c = cost[p[j] - 1][j - 1];
    if (c >= kInfCost) return kInfCost;
    total += c;
  }
  return total;
}

class Backward {
 public:
  Backward(const Board& board, const ThreadConfig& config)
      : b_(board), cfg_(config), n_(board.box_count) {
    box_here_.assign(b_.area, 0);
    reach_.assign(b_.area, 0);
    visited_.assign(b_.area, 0);
    queue_buf_.assign(b_.area, 0);
    BuildStartDistances();
  }

  BackwardResult Run(const std::atomic<bool>& stop);

 private:
  struct Node {
    u64 hash;
    u32 parent;
    u32 g;
    u16 norm;
    u16 moved_from;
    u16 moved_to;
    u16 switches;
  };

  struct HeapItem {
    u64 priority;
    u32 idx;
  };

  // fwd_dist_[s][c]: pushes that move a lone box from start cell s to c, with the player
  // allowed to stand on any floor cell. A pulled box must stay where some start can reach.
  void BuildStartDistances() {
    starts_.assign(b_.initial_boxes.begin(), b_.initial_boxes.begin() + n_);
    start_idx_.assign(b_.area, kNoGoal);
    for (int s = 0; s < n_; ++s) start_idx_[starts_[s]] = static_cast<u8>(s);

    fwd_dist_.assign(n_, std::vector<int>(b_.area, kInfCost));
    reachable_.assign(b_.area, 0);
    std::vector<u16> q;
    for (int s = 0; s < n_; ++s) {
      std::vector<int>& dist = fwd_dist_[s];
      q.clear();
      dist[starts_[s]] = 0;
      q.push_back(starts_[s]);
      for (size_t head = 0; head < q.size(); ++head) {
        const u16 c = q[head];
        for (int d = 0; d < 4; ++d) {
          const u16 to = static_cast<u16>(c + b_.dirs[d]);
          const u16 stand = static_cast<u16>(c - b_.dirs[d]);
          if (b_.IsWall(to) || b_.IsWall(stand) || dist[to] != kInfCost) continue;
          dist[to] = dist[c] + 1;
          q.push_back(to);
        }
      }
      for (int c = 0; c < b_.area; ++c) {
        if (dist[c] < kInfCost) reachable_[c] = 1;
      }
    }
  }

  template <bool kTrackSecond>
  u16 Flood(u16 from, u32* marks, u32 seq) noexcept {
    int head = 0, tail = 0;
    u16* q = queue_buf_.data();
    q[tail++] = from;
    marks[from] = seq;
    u16 lowest = kUnreach;
    u16 second = kUnreach;
    while (head < tail) {
      const u16 c = q[head++];
      if constexpr (kTrackSecond) {
        if (c < lowest) {
          second = lowest;
          lowest = c;
        } else if (c < second) {
          second = c;
        }
      } else {
        if (c < lowest) lowest = c;
      }
      for (int d = 0; d < 4; ++d) {
        const u16 nb = static_cast<u16>(c + b_.dirs[d]);
        if (!b_.IsWall(nb) && !box_here_[nb] && marks[nb] != seq) {
          marks[nb] = seq;
          q[tail++] = nb;
        }
      }
    }
    if constexpr (kTrackSecond) second_norm_ = second;
    return lowest;
  }

  u16 FloodParent(u16 from) noexcept {
    if (reach_seq_ > 0xFF000000u) {
      std::fill(reach_.begin(), reach_.end(), 0);
      reach_seq_ = 0;
    }
    return Flood<true>(from, reach_.data(), ++reach_seq_);
  }

  u16 FloodChild(u16 from) noexcept {
    if (visit_seq_ > 0xFF000000u) {
      std::fill(visited_.begin(), visited_.end(), 0);
      visit_seq_ = 0;
    }
    return Flood<false>(from, visited_.data(), ++visit_seq_);
  }

  int Heuristic(const u16* boxes) const {
    u32 used_s = 0, used_b = 0;
    for (int i = 0; i < n_; ++i) {
      const u8 s = start_idx_[boxes[i]];
      if (s != kNoGoal) {
        used_s |= 1u << s;
        used_b |= 1u << i;
      }
    }
    int ub[kMaxBoxes], us[kMaxBoxes];
    int m = 0, ms = 0;
    for (int i = 0; i < n_; ++i) {
      if (!(used_b & (1u << i))) ub[m++] = i;
    }
    for (int s = 0; s < n_; ++s) {
      if (!(used_s & (1u << s))) us[ms++] = s;
    }
    int cost[kMaxBoxes][kMaxBoxes];
    for (int i = 0; i < m; ++i) {
      const u16 bpos = boxes[ub[i]];
      for (int j = 0; j < m; ++j) cost[i][j] = fwd_dist_[us[j]][bpos];
    }
    return MinCostMatching(m, cost);
  }

  u64 Hash(const u16* boxes, u16 norm) const noexcept {
    u64 h = b_.zobrist_player[norm];
    for (int i = 0; i < n_; ++i) h ^= b_.zobrist_box[boxes[i]];
    return h;
  }

  const u16* BoxesOf(u32 idx) const noexcept {
    return &boxes_[static_cast<size_t>(idx) * n_];
  }

  bool Probe(const u16* boxes, u16 norm, u64 hash, size_t* out_slot) const noexcept {
    size_t slot = static_cast<size_t>(hash) & table_mask_;
    const u32 tag = static_cast<u32>(hash >> 56);
    while (table_[slot] != 0) {
      const u32 entry = table_[slot];
      if ((entry >> 24) == tag) {
        const u32 other = entry & 0xFFFFFFu;
        const Node& ex = nodes_[other];
        if (ex.hash == hash && ex.norm == norm &&
            std::memcmp(BoxesOf(other), boxes, n_ * sizeof(u16)) == 0) {
          return false;
        }
      }
      slot = (slot + 1) & table_mask_;
    }
    *out_slot = slot;
    return true;
  }

  u32 Commit(size_t slot, const u16* boxes, u16 norm, u64 hash, u32 parent, u16 from, u16 to,
             u32 g, u16 switches) {
    const u32 idx = static_cast<u32>(nodes_.size());
    boxes_.insert(boxes_.end(), boxes, boxes + n_);
    nodes_.push_back({hash, parent, g, norm, from, to, switches});
    table_[slot] = (static_cast<u32>(hash >> 56) << 24) | idx;
    return idx;
  }

  void HeapPush(u32 idx, u64 priority) {
    int i = static_cast<int>(heap_.size());
    heap_.push_back({priority, idx});
    while (i > 0) {
      const int p = (i - 1) >> 2;
      if (heap_[p].priority <= priority) break;
      heap_[i] = heap_[p];
      i = p;
    }
    heap_[i] = {priority, idx};
  }

  u32 HeapPop() noexcept {
    const u32 ret = heap_[0].idx;
    const HeapItem tmp = heap_.back();
    heap_.pop_back();
    const int n = static_cast<int>(heap_.size());
    if (n == 0) return ret;
    int i = 0;
    while (true) {
      const int first_child = (i << 2) + 1;
      if (first_child >= n) break;
      const int last_child = std::min(first_child + 4, n);
      int best_child = first_child;
      u64 best_priority = heap_[first_child].priority;
      for (int c = first_child + 1; c < last_child; ++c) {
        if (heap_[c].priority < best_priority) {
          best_priority = heap_[c].priority;
          best_child = c;
        }
      }
      if (tmp.priority <= best_priority) break;
      heap_[i] = heap_[best_child];
      i = best_child;
    }
    heap_[i] = tmp;
    return ret;
  }

  std::string Reconstruct(u32 idx);

#ifdef SOKO_TRACE
  void Trace(TraceKind kind, TraceReason reason, u32 node, u32 parent, u16 from, u16 to, u32 g,
             u32 h) const {
    if (!g_trace_event) return;
    if (from == to) {  // no move: the virtual root's children
      g_trace_event({kind, reason, node, parent, 255, 255, 255, 255, g, h});
      return;
    }
    g_trace_event({kind, reason, node, parent, b_.row_of[from] - 2, b_.col_of[from] - 2,
                   b_.row_of[to] - 2, b_.col_of[to] - 2, g, h});
  }
#define SOKO_BACKWARD_TRACE(...) Trace(__VA_ARGS__)
#else
#define SOKO_BACKWARD_TRACE(...) ((void)0)
#endif

  // Doubles the hash table and reinserts every state. The table starts small so easy maps
  // do not pay for a large allocation.
  void Grow() {
    const size_t size = table_.empty() ? (size_t{1} << 12) : table_.size() * 2;
    table_.assign(size, 0);
    table_mask_ = size - 1;
    for (u32 idx = 1; idx < nodes_.size(); ++idx) {
      const u64 h = nodes_[idx].hash;
      size_t slot = static_cast<size_t>(h) & table_mask_;
      while (table_[slot] != 0) slot = (slot + 1) & table_mask_;
      table_[slot] = (static_cast<u32>(h >> 56) << 24) | idx;
    }
  }

  const Board& b_;
  const ThreadConfig& cfg_;
  const int n_;
  std::vector<u16> starts_;
  std::vector<u8> start_idx_;
  std::vector<std::vector<int>> fwd_dist_;
  std::vector<u8> reachable_;
  std::vector<u8> box_here_;
  std::vector<u32> reach_;
  std::vector<u32> visited_;
  std::vector<u16> queue_buf_;
  u32 reach_seq_ = 0;
  u32 visit_seq_ = 0;
  u16 second_norm_ = kUnreach;

  std::vector<u16> boxes_;
  std::vector<Node> nodes_;
  std::vector<u32> table_;
  size_t table_mask_ = 0;
  std::vector<HeapItem> heap_;
  u32 prng_ = 2463534242u;
};

BackwardResult Backward::Run(const std::atomic<bool>& stop) {
  BackwardResult result;
  const u32 limit =
      std::min<u32>(cfg_.backward_state_limit ? cfg_.backward_state_limit : 4'000'000u, 0xFFFFFEu);

  // Dummy node 0 so valid state indices start at 1 (table_ uses 0 for empty).
  nodes_.push_back({});
  boxes_.resize(n_, 0);
  Grow();

  // Target: the initial layout, with the player in the initial player's region.
  std::vector<u16> target(starts_);
  std::sort(target.begin(), target.end());
  for (const u16 c : target) box_here_[c] = 1;
  const u16 target_norm = FloodParent(b_.initial_player);
  for (const u16 c : target) box_here_[c] = 0;

  auto priority = [&](int h, u32 g, u16 switches) {
    prng_ ^= prng_ << 13;
    prng_ ^= prng_ >> 17;
    prng_ ^= prng_ << 5;
    return static_cast<u64>(h) * cfg_.h_weight + static_cast<u64>(switches) * cfg_.switch_penalty +
           static_cast<u64>(g) * cfg_.g_weight + (prng_ % 100);
  };

  // Roots: every goal on a box, the player in each open region that touches playable floor.
#ifdef SOKO_TRACE
  std::vector<u32> root_nodes;
#endif
  std::vector<u16> solved(b_.goals.begin(), b_.goals.begin() + n_);
  std::sort(solved.begin(), solved.end());
  for (const u16 c : solved) box_here_[c] = 1;
  const int root_h = Heuristic(solved.data());
  if (root_h < kInfCost) {
    std::vector<u8> taken(b_.area, 0);
    for (int i = 0; i < b_.playable_count; ++i) {
      const u16 c = b_.playable_cells[i];
      if (box_here_[c] || taken[c]) continue;
      const u16 norm = FloodParent(c);
      const u32 seq = reach_seq_;
      for (int k = 0; k < b_.area; ++k) {
        if (reach_[k] == seq) taken[k] = 1;
      }
      const u64 hash = Hash(solved.data(), norm);
      size_t slot = 0;
      if (Probe(solved.data(), norm, hash, &slot)) {
        const u32 idx = Commit(slot, solved.data(), norm, hash, 0, 0, 0, 0, 0);
        HeapPush(idx, priority(root_h, 0, 0));
#ifdef SOKO_TRACE
        root_nodes.push_back(idx);
#endif
      }
    }
  }
  for (const u16 c : solved) box_here_[c] = 0;

#ifdef SOKO_TRACE
  if (!root_nodes.empty()) {
    SOKO_BACKWARD_TRACE(TraceKind::kPop, TraceReason::kNone, kVirtualRoot, 0, 0, 0, 0, 0);
    for (const u32 idx : root_nodes) {
      SOKO_BACKWARD_TRACE(TraceKind::kChild, TraceReason::kNone, idx, kVirtualRoot, 0, 0, 0,
                          static_cast<u32>(root_h));
    }
  }
#endif

  std::vector<u16> child(n_);
  [[maybe_unused]] u32 iter = 0;
  while (!heap_.empty()) {
#ifdef SOKO_TRACE
    // Poll every pop so --pops and the time limit stop the trace exactly.
    if (stop.load(std::memory_order_relaxed)) break;
#else
    if ((++iter & 127) == 0 && stop.load(std::memory_order_relaxed)) break;
#endif
    const u32 idx = HeapPop();
    ++result.expansions;
    SOKO_BACKWARD_TRACE(TraceKind::kPop, TraceReason::kNone, idx, 0, 0, 0, 0, 0);

    if ((nodes_.size() + static_cast<size_t>(4 * n_) + 1) * 2 > table_.size()) Grow();

    const Node curr_node = nodes_[idx];
    const u16* boxes = BoxesOf(idx);
    for (int i = 0; i < n_; ++i) box_here_[boxes[i]] = 1;
    const u16 parent_norm = FloodParent(curr_node.norm);
    const u16 parent_second_norm = second_norm_;
    const u32 region_seq = reach_seq_;

    const u32 parent_g = curr_node.g;
    const u16 parent_switches = curr_node.switches;
    const u16 parent_last = curr_node.moved_to;

    for (int i = 0; i < n_; ++i) {
      const u16 box = BoxesOf(idx)[i];
      for (int d = 0; d < 4; ++d) {
        const u16 q = static_cast<u16>(box + b_.dirs[d]);  // player cell, box moves here
        if (reach_[q] != region_seq) continue;
        const u16 r = static_cast<u16>(q + b_.dirs[d]);    // player steps back to here
        if (b_.IsWall(r) || box_here_[r] || !reachable_[q]) continue;

        bool merges_outside = false;
        bool box_touches_region = false;
        for (int k = 0; k < 4; ++k) {
          if (k == d) continue;
          const u16 nb = static_cast<u16>(box + b_.dirs[k]);
          if (!b_.IsWall(nb) && !box_here_[nb]) {
            if (reach_[nb] == region_seq) {
              box_touches_region = true;
            } else {
              merges_outside = true;
              break;
            }
          }
        }

        u16 norm = 0;
        bool fast_path = !merges_outside;
        if (fast_path) {
          unsigned mask = 0;
          for (int ri = 0; ri < 8; ++ri) {
            const bool in = (reach_[static_cast<u16>(q + b_.ring[ri])] == region_seq);
            mask |= static_cast<unsigned>(in) << ri;
          }
          fast_path = kCutVertexOk[mask];
        }

        if (fast_path) {
          const u16 region_min = (parent_norm != q) ? parent_norm : parent_second_norm;
          norm = box_touches_region ? std::min(region_min, box) : region_min;
        } else {
          box_here_[box] = 0;
          box_here_[q] = 1;
          norm = FloodChild(r);
          box_here_[q] = 0;
          box_here_[box] = 1;
        }

        const u16* cur = BoxesOf(idx);
        int w = 0;
        bool placed = false;
        for (int k = 0; k < n_; ++k) {
          if (k == i) continue;
          if (!placed && q < cur[k]) {
            child[w++] = q;
            placed = true;
          }
          child[w++] = cur[k];
        }
        if (!placed) child[w++] = q;

        const u64 hash = Hash(child.data(), norm);
        size_t slot = 0;
        if (!Probe(child.data(), norm, hash, &slot)) {
          SOKO_BACKWARD_TRACE(TraceKind::kPruned, TraceReason::kDuplicate, 0, idx, box, q, 0, 0);
          continue;
        }

        const int h = Heuristic(child.data());
        if (h >= kInfCost) {
          SOKO_BACKWARD_TRACE(TraceKind::kPruned, TraceReason::kDeadHeuristic, 0, idx, box, q, 0,
                              0);
          continue;
        }
        if (nodes_.size() >= limit + 1) {
          result.pool_exhausted = true;
          for (int k = 0; k < n_; ++k) box_here_[BoxesOf(idx)[k]] = 0;
          return result;
        }
        const u16 sw = static_cast<u16>(parent_switches +
                                        ((parent_last != 0 && parent_last != box) ? 1 : 0));
        const u32 next = Commit(slot, child.data(), norm, hash, idx, box, q, parent_g + 1, sw);

        if (norm == target_norm && std::equal(child.begin(), child.end(), target.begin())) {
          SOKO_BACKWARD_TRACE(TraceKind::kSolution, TraceReason::kNone, next, idx, box, q,
                              parent_g + 1, static_cast<u32>(h));
          for (int k = 0; k < n_; ++k) box_here_[BoxesOf(idx)[k]] = 0;
          result.solved = true;
          result.moves = Reconstruct(next);
          return result;
        }
        SOKO_BACKWARD_TRACE(TraceKind::kChild, TraceReason::kNone, next, idx, box, q,
                            parent_g + 1, static_cast<u32>(h));
        HeapPush(next, priority(h, parent_g + 1, sw));
      }
    }
    for (int k = 0; k < n_; ++k) box_here_[BoxesOf(idx)[k]] = 0;
  }
  return result;
}

// Replays the pulls backwards as pushes from the initial position, adding the walks.
std::string Backward::Reconstruct(u32 idx) {
  std::vector<u32> chain;
  for (u32 s = idx; nodes_[s].parent != 0; s = nodes_[s].parent) chain.push_back(s);
  // chain[0] is the last pull; replaying in this order goes from the initial layout forward.

  std::fill(box_here_.begin(), box_here_.end(), 0);
  for (const u16 c : starts_) box_here_[c] = 1;
  u16 player = b_.initial_player;
  std::string moves;
  std::vector<int> came(b_.area, -1);
  std::vector<u16> q;

  for (const u32 s : chain) {
    // The pull moved a box from `from` to `to` (toward the player). Forward, the box goes
    // from `to` back to `from`, pushed by a player standing just past `to`.
    const u16 from = nodes_[s].moved_from;
    const u16 to = nodes_[s].moved_to;
    const int pull_dir = [&] {
      for (int d = 0; d < 4; ++d) {
        if (static_cast<u16>(from + b_.dirs[d]) == to) return d;
      }
      return 0;
    }();
    const int push_dir = pull_dir ^ 1;
    const u16 stand = static_cast<u16>(to + b_.dirs[pull_dir]);

    std::fill(came.begin(), came.end(), -1);
    q.assign(1, player);
    came[player] = 4;
    for (size_t head = 0; head < q.size() && came[stand] < 0; ++head) {
      const u16 c = q[head];
      for (int d = 0; d < 4; ++d) {
        const u16 nb = static_cast<u16>(c + b_.dirs[d]);
        if (b_.IsWall(nb) || box_here_[nb] || came[nb] >= 0) continue;
        came[nb] = d;
        q.push_back(nb);
      }
    }
    std::string walk;
    for (u16 c = stand; c != player;) {
      const int d = came[c];
      if (d < 0 || d > 3) return std::string();  // should not happen: no walk to the push
      walk.push_back(kMoveChar[d]);
      c = static_cast<u16>(c - b_.dirs[d]);
    }
    std::reverse(walk.begin(), walk.end());
    moves += walk;
    moves.push_back(kMoveChar[push_dir]);
    box_here_[to] = 0;
    box_here_[from] = 1;
    player = to;
  }
  return moves;
}

}  // namespace

BackwardResult BackwardSearch(const Board& board, const ThreadConfig& config,
                              const std::atomic<bool>& stop) {
  Backward search(board, config);
  return search.Run(stop);
}

}  // namespace soko
