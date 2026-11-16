#include "soko/goal_room.h"

#include <algorithm>
#include <vector>

namespace soko {

GoalRoom::GoalRoom(const Board& board, const ThreadConfig& config)
    : board_(board), config_(config) {
  if (config_.goal_room_macro) Build();
}

namespace {

constexpr int kMaxRoomCells = 256;
constexpr char kMoveChar[4] = {'u', 'd', 'l', 'r'};

}  // namespace

// Finds a "goal room": a region holding goals that the rest of the board reaches only
// through one gate cell, with no box or player inside at the start. Among candidates it
// keeps the one holding the most goals, then the smallest. It then searches for an order
// in which boxes pushed in through the gate can fill every goal of the room.
void GoalRoom::Build() {
  ok_ = false;
  std::vector<int> mark(board_.area, 0);
  int stamp = 0;
  int best_goals = 1;  // need at least two goals for a room to be worth it
  int best_size = 1 << 30;
  u16 best_gate = 0;
  std::vector<u16> best_cells;

  std::vector<bool> start_box(board_.area, false);
  for (int i = 0; i < board_.box_count; ++i) start_box[board_.initial_boxes[i]] = true;

  for (int pi = 0; pi < board_.playable_count; ++pi) {
    const u16 gate = board_.playable_cells[pi];
    if (start_box[gate] || gate == board_.initial_player) continue;

    for (int d = 0; d < 4; ++d) {
      const u16 seed = static_cast<u16>(gate + board_.dirs[d]);
      if (board_.IsWall(seed) || mark[seed] == stamp + 1) continue;

      ++stamp;
      std::vector<u16> cells;
      int head = 0;
      cells.push_back(seed);
      mark[seed] = stamp;
      bool valid = true;
      while (head < static_cast<int>(cells.size()) && valid) {
        const u16 c = cells[head++];
        if (start_box[c] || c == board_.initial_player) valid = false;
        for (int k = 0; k < 4 && valid; ++k) {
          const u16 n = static_cast<u16>(c + board_.dirs[k]);
          if (n == gate || board_.IsWall(n) || mark[n] == stamp) continue;
          mark[n] = stamp;
          cells.push_back(n);
          if (static_cast<int>(cells.size()) > kMaxRoomCells) valid = false;
        }
      }
      if (!valid) continue;

      // The gate must touch the room through exactly one cell (seed), so boxes have a
      // single way in.
      int touching = 0;
      for (int k = 0; k < 4; ++k) touching += mark[gate + board_.dirs[k]] == stamp ? 1 : 0;
      if (touching != 1) continue;

      int goals = 0;
      for (const u16 c : cells) goals += board_.is_goal[c] ? 1 : 0;
      const int size = static_cast<int>(cells.size());
      if (goals > best_goals || (goals == best_goals && goals > 1 && size < best_size)) {
        best_goals = goals;
        best_size = size;
        best_gate = gate;
        best_cells = cells;
      }
    }
  }
  if (best_cells.empty()) return;

  // The entry is the single room cell next to the gate; boxes come in through it.
  int entry = -1;
  for (int d = 0; d < 4; ++d) {
    const u16 n = static_cast<u16>(best_gate + board_.dirs[d]);
    if (std::find(best_cells.begin(), best_cells.end(), n) != best_cells.end()) {
      if (entry >= 0) return;
      entry = n;
    }
  }
  // A box is pushed into the entry from the gate, so the cell behind the gate must be floor.
  // An entry that is itself a goal is fine: the packing order fills it last.
  const int into = entry - best_gate;
  if (board_.IsWall(best_gate - into)) return;

  gate_ = best_gate;
  entry_ = static_cast<u16>(entry);
  cells_ = best_cells;
  cells_.push_back(gate_);  // the player may stand on the gate
  local_.assign(board_.area, -1);
  for (int i = 0; i < static_cast<int>(cells_.size()); ++i) local_[cells_[i]] = i;
  nbr_.assign(cells_.size(), {-1, -1, -1, -1});
  for (int i = 0; i < static_cast<int>(cells_.size()); ++i) {
    for (int d = 0; d < 4; ++d) nbr_[i][d] = local_[cells_[i] + board_.dirs[d]];
  }

  goal_count_ = 0;
  for (const u16 c : best_cells) {
    if (board_.is_goal[c]) order_[goal_count_++] = c;
  }

  std::vector<u32> failed;
  if (!PackRoom(0, 0, failed)) return;

  for (const u16 c : best_cells) in_room_[c] = true;
  ok_ = true;

  BuildEntryDistances();
  heuristic_enabled_ = config_.room_heuristic && goal_count_ == board_.goal_count;

  if (config_.delivery_macro) {
    dl_seen_.assign(static_cast<size_t>(board_.area) * 4, 0);
    dl_parent_.assign(static_cast<size_t>(board_.area) * 4, -1);
    dl_dir_.assign(static_cast<size_t>(board_.area) * 4, 0);
    dl_queue_.assign(static_cast<size_t>(board_.area) * 4, 0);
    dl_flood_.assign(board_.area, 0);
    dl_stack_.assign(board_.area, 0);
  }
}

// Pull distances to the entry: how many pushes bring a box from each cell to it.
void GoalRoom::BuildEntryDistances() {
  entry_dist_.assign(board_.area, kUnreach);
  std::vector<u16> queue = {entry_};
  entry_dist_[entry_] = 0;
  for (size_t h = 0; h < queue.size(); ++h) {
    const u16 curr = queue[h];
    for (int d = 0; d < 4; ++d) {
      const u16 from = static_cast<u16>(curr - board_.dirs[d]);
      const u16 stand = static_cast<u16>(from - board_.dirs[d]);
      if (board_.IsWall(from) || board_.IsWall(stand) || in_room_[from]) continue;
      if (entry_dist_[from] <= entry_dist_[curr] + 1) continue;
      entry_dist_[from] = static_cast<u16>(entry_dist_[curr] + 1);
      queue.push_back(from);
    }
  }
}

// Breadth-first search over (box cell, side the player stands on) for a push sequence that
// brings `box` to the room entry, the other boxes staying put. Expects `reach` to hold
// the current boxes and the current player region.
bool GoalRoom::DeliveryPath(Reachability& reach, u16 box,
                            std::vector<std::pair<u16, u8>>* pushes, int* push_count,
                            u8* last_dir) {
  if (dl_seq_ > 0xF0000000u) {
    std::fill(dl_seen_.begin(), dl_seen_.end(), 0);
    std::fill(dl_flood_.begin(), dl_flood_.end(), 0);
    dl_seq_ = 0;
  }
  const u32 seq = ++dl_seq_;
  const bool was_blocked = reach.Blocked(box);
  reach.SetBlocked(box, false);  // the moving box is tracked by the search state instead

  bool found = false;
  int found_state = -1;
  u16 found_box = 0;
  u8 found_dir = 0;
  int head = 0, tail = 0;

  auto try_push = [&](u16 b, int d, int from_state) {
    const u16 to = static_cast<u16>(b + board_.dirs[d]);
    if (reach.Blocked(to)) return;
    if (in_room_[to]) {
      if (to != entry_) return;
      found = true;
      found_state = from_state;
      found_box = b;
      found_dir = static_cast<u8>(d);
      return;
    }
    if (board_.min_dist[to] >= kUnreach) return;
    const int state = to * 4 + (d ^ 1);  // the player now stands behind the box
    if (dl_seen_[state] == seq) return;
    dl_seen_[state] = seq;
    dl_parent_[state] = from_state;
    dl_dir_[state] = static_cast<u8>(d);
    dl_queue_[tail++] = state;
  };

  for (int d = 0; d < 4 && !found; ++d) {
    if (reach.Reaches(static_cast<u16>(box - board_.dirs[d]))) try_push(box, d, -1);
  }

  u32 processed = 0;
  while (!found && head < tail && processed++ < config_.delivery_state_limit) {
    const int state = dl_queue_[head++];
    const u16 b = static_cast<u16>(state >> 2);
    const u16 player = static_cast<u16>(b + board_.dirs[state & 3]);

    const u32 flood = ++dl_seq_;
    int top = 0;
    dl_stack_[top++] = player;
    dl_flood_[player] = flood;
    while (top > 0) {
      const u16 c = dl_stack_[--top];
      for (int d = 0; d < 4; ++d) {
        const u16 n = static_cast<u16>(c + board_.dirs[d]);
        if (n == b || reach.Blocked(n) || dl_flood_[n] == flood) continue;
        dl_flood_[n] = flood;
        dl_stack_[top++] = n;
      }
    }
    for (int d = 0; d < 4 && !found; ++d) {
      if (dl_flood_[static_cast<u16>(b - board_.dirs[d])] == flood) try_push(b, d, state);
    }
  }
  reach.SetBlocked(box, was_blocked);
  if (!found) return false;

  int count = 1;
  for (int s = found_state; s >= 0; s = dl_parent_[s]) ++count;
  if (push_count) *push_count = count;
  if (last_dir) *last_dir = found_dir;
  if (pushes) {
    pushes->clear();
    pushes->push_back({found_box, found_dir});
    for (int s = found_state; s >= 0; s = dl_parent_[s]) {
      const u8 d = dl_dir_[s];
      pushes->push_back({static_cast<u16>((s >> 2) - board_.dirs[d]), d});
    }
    std::reverse(pushes->begin(), pushes->end());
  }
  return true;
}

int GoalRoom::RoomHeuristic(const u16* boxes, u8* out_goal_of) const {
  int h = 0;
  int filled = 0;
  for (int b = 0; b < board_.box_count; ++b) filled += in_room_[boxes[b]] ? 1 : 0;
  int next = filled;
  for (int b = 0; b < board_.box_count; ++b) {
    const u16 pos = boxes[b];
    if (in_room_[pos]) {
      if (out_goal_of) out_goal_of[b] = board_.goal_idx[pos];
      continue;
    }
    const int d = entry_dist_[pos];
    if (d >= kUnreach) return kDeadHeuristic;
    h += d + 1;  // +1: the push from the gate into the entry
    if (out_goal_of) out_goal_of[b] = board_.goal_idx[order_[next++]];
  }
  for (int k = filled; k < goal_count_; ++k) h += push_len_[k];
  return h;
}

// Plans the box at local cell `box` (player at `player`) onto `goal` using only room
// cells, then requires the player to get back to the gate. Writes the moves if asked.
bool GoalRoom::RoomPath(int box, int player, int goal, const std::vector<char>& occupied,
                      std::string* moves, int* pushes, int* final_player) const {
  const int n = static_cast<int>(cells_.size());
  const int gate = local_[gate_];
  std::vector<int> parent(n * n, -2);
  std::vector<u8> step(n * n, 0);
  std::vector<int> queue;
  const int start = box * n + player;
  parent[start] = -1;
  queue.push_back(start);

  std::vector<int> seen(n, 0);
  int seen_stamp = 0;
  auto reaches_gate = [&](int from, int b) {
    ++seen_stamp;
    std::vector<int> stack = {from};
    seen[from] = seen_stamp;
    while (!stack.empty()) {
      const int c = stack.back();
      stack.pop_back();
      if (c == gate) return true;
      for (int d = 0; d < 4; ++d) {
        const int m = nbr_[c][d];
        if (m < 0 || m == b || occupied[m] || seen[m] == seen_stamp) continue;
        seen[m] = seen_stamp;
        stack.push_back(m);
      }
    }
    return false;
  };

  for (int head = 0; head < static_cast<int>(queue.size()); ++head) {
    const int state = queue[head];
    const int b = state / n;
    const int p = state % n;
    if (b == goal && reaches_gate(p, b)) {
      std::string path;
      int count = 0;
      for (int s = state; parent[s] >= 0; s = parent[s]) {
        path.push_back(kMoveChar[step[s] & 3]);
        count += step[s] >> 2;
      }
      std::reverse(path.begin(), path.end());
      if (moves) *moves = path;
      if (pushes) *pushes = count;
      if (final_player) *final_player = cells_[p];
      return true;
    }
    for (int d = 0; d < 4; ++d) {
      const int np = nbr_[p][d];
      if (np < 0 || occupied[np]) continue;
      int nb = b;
      bool pushed = false;
      if (np == b) {
        nb = nbr_[b][d];
        if (nb < 0 || nb == gate || occupied[nb]) continue;
        pushed = true;
      }
      const int next = nb * n + np;
      if (parent[next] != -2) continue;
      parent[next] = state;
      step[next] = static_cast<u8>(d | (pushed ? 4 : 0));
      queue.push_back(next);
    }
  }
  return false;
}

// Depth-first search for a fill order: with goals in `filled` already holding boxes, try
// each open goal (deepest first) that a fresh box at the entry can reach.
bool GoalRoom::PackRoom(u32 filled, int placed, std::vector<u32>& failed) {
  if (placed == goal_count_) return true;
  if (std::find(failed.begin(), failed.end(), filled) != failed.end()) return false;

  const int n = static_cast<int>(cells_.size());
  std::vector<char> occupied(n, 0);
  std::vector<int> goal_ids;
  for (int g = 0; g < goal_count_; ++g) {
    // order_ holds the room's goals in cell order until the search reorders it,
    // so keep the canonical list separately.
    goal_ids.push_back(g);
  }
  static thread_local std::vector<u16> canonical;
  if (placed == 0) canonical.assign(order_.begin(), order_.begin() + goal_count_);
  for (int g = 0; g < goal_count_; ++g) {
    if (filled & (1u << g)) occupied[local_[canonical[g]]] = 1;
  }

  // Deepest first: farthest from the entry by walking distance inside the room.
  std::vector<int> dist(n, 1 << 20);
  std::vector<int> bfs = {local_[entry_]};
  dist[bfs[0]] = 0;
  for (size_t h = 0; h < bfs.size(); ++h) {
    for (int d = 0; d < 4; ++d) {
      const int m = nbr_[bfs[h]][d];
      if (m >= 0 && dist[m] > dist[bfs[h]] + 1) {
        dist[m] = dist[bfs[h]] + 1;
        bfs.push_back(m);
      }
    }
  }
  std::sort(goal_ids.begin(), goal_ids.end(), [&](int a, int b) {
    return dist[local_[canonical[a]]] > dist[local_[canonical[b]]];
  });

  const int entry = local_[entry_];
  const int gate = local_[gate_];
  if (occupied[entry]) {  // the entry goal is filled: no further box can come in
    failed.push_back(filled);
    return false;
  }
  for (const int g : goal_ids) {
    if (filled & (1u << g)) continue;
    int pushes = 0;
    if (!RoomPath(entry, gate, local_[canonical[g]], occupied, nullptr, &pushes, nullptr))
      continue;
    if (PackRoom(filled | (1u << g), placed + 1, failed)) {
      order_[placed] = canonical[g];
      push_len_[placed] = static_cast<u16>(pushes);
      return true;
    }
  }
  failed.push_back(filled);
  return false;
}

bool GoalRoom::ReplayPlacement(const Reachability& reach, u16 player, u16 goal,
                               std::string* moves, u16* final_player) const {
  std::vector<char> occupied(cells_.size(), 0);
  for (size_t i = 0; i < cells_.size(); ++i) {
    occupied[i] = reach.IsBox(cells_[i]) && cells_[i] != entry_;
  }
  int end_player = player;
  if (!RoomPath(local_[entry_], local_[player], local_[goal], occupied, moves, nullptr,
                &end_player))
    return false;
  *final_player = static_cast<u16>(end_player);
  return true;
}

}  // namespace soko
