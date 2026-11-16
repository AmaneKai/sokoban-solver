#include "solver.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "board.h"
#include "deadlock.h"
#include "flags.h"
#include "heuristic.h"

std::atomic<uint64_t> g_nodes_explored{0};
std::atomic<bool> g_solution_found{false};

namespace {

constexpr uint64_t kHeuristicScale = 1'000'000;

struct State {
  std::array<int, kMaxBoxes> boxes{};
  std::array<int, kMaxBoxes> goal_of{};
  int player_cell = 0;
  int g = 0;
  int switches = 0;
  int last_box_pos = 0;
  int push_from = 0;
  int push_dir = 0;
  int macro_steps = 1;
  int parent_index = 0;
  uint64_t hash = 0;
};

struct PriorityNode {
  uint64_t priority = 0;
  int state_index = 0;

  bool operator>(const PriorityNode& other) const {
    return priority > other.priority;
  }
};

class ZobristHasher {
 public:
  void Initialize(int area) {
    std::mt19937_64 rng(1337);

    for (int i = 0; i < area; ++i) {
      box_keys_[i] = rng();
      player_keys_[i] = rng();
    }

  }

  uint64_t box_key(int cell) const { 
    return box_keys_[cell]; 
  }

  uint64_t player_key(int cell) const { 
    return player_keys_[cell]; 
  }

 private:
  std::array<uint64_t, kMaxCells> box_keys_{};
  std::array<uint64_t, kMaxCells> player_keys_{};
};

class SokobanSolver {
 public:
  SokobanSolver(int width, int height, char** map, char** items);

  std::string Solve();

 private:
  void InitializeSearch();
  bool ExpandCurrentState(const State& curr_state, int curr_index);
  bool TryPushBox(const State& curr_state, int curr_index, int bi, int d, const PiCorralResult& corral, int parent_norm);

  int ApplyTunnelMacro(int dest, int dir, int& out_macro_steps) const;
  int CalculateNextNormalizedPlayer(int dest, int dir, int orig_box_pos,
                                    int parent_norm, int perp1, int perp2);
  int ComputeNormalizedPlayer(int start_cell, bool fill_walk_dist);
  bool IsVisited(const State& state) const;
  void RecordVisited(const State& state, int state_index);
  std::string ReconstructSolution(int goal_state_index);

  Board board_;
  ZobristHasher hasher_;
  DeadlockDetector deadlock_;

  int region_size_ = 0;
  int region_second_norm_ = -1;

  std::array<uint32_t, kMaxCells> reach_{};
  uint32_t reach_seq_ = 0;
  std::array<uint32_t, kMaxCells> visited_{};
  uint32_t visited_seq_ = 0;

  std::array<int, kMaxCells> walk_dist_{};
  std::array<int, kMaxCells> bfs_q_{};

  std::vector<State> state_pool_;
  std::unordered_map<uint64_t, std::vector<int>> visited_table_;
  std::priority_queue<PriorityNode, std::vector<PriorityNode>, std::greater<PriorityNode>> open_set_;

  std::string solution_;
};

SokobanSolver::SokobanSolver(int width, int height, char** map, char** items) {
  board_.Initialize(width, height, map, items);
  board_.PrecomputePullDistances();
  board_.BuildFreezeStaticTable();
  hasher_.Initialize(board_.area());
}

int SokobanSolver::ComputeNormalizedPlayer(int start_cell, bool fill_walk_dist) {
  int head = 0, tail = 0;
  ++reach_seq_;
  bfs_q_[tail++] = start_cell;
  reach_[start_cell] = reach_seq_;

  if (fill_walk_dist) {
    walk_dist_[start_cell] = 0;
  }

  int canonical_player = start_cell;
  int second_canonical = kUnreachableDistance;

  while (head < tail) {
    const int curr = bfs_q_[head++];
    if (curr < canonical_player) {
      second_canonical = canonical_player;
      canonical_player = curr;
    } else if (curr < second_canonical) {
      second_canonical = curr;
    }

    for (int d = 0; d < 4; ++d) {
      const int next = curr + board_.dir_offset(d);
      if (!board_.blocked(next) && reach_[next] != reach_seq_) {
        reach_[next] = reach_seq_;
        if (fill_walk_dist) {
          walk_dist_[next] = walk_dist_[curr] + 1;
        }
        bfs_q_[tail++] = next;
      }
    }
  }

  region_second_norm_ = second_canonical;
  region_size_ = tail;
  return canonical_player;
}

bool SokobanSolver::IsVisited(const State& state) const {
  auto it = visited_table_.find(state.hash);
  if (it == visited_table_.end()) {
    return false;
  }

  for (int index : it->second) {
    const State& existing = state_pool_[index];
    if (existing.player_cell == state.player_cell &&
        std::memcmp(existing.boxes.data(), state.boxes.data(),
                    board_.box_count() * sizeof(int)) == 0) {
      return true;
    }
  }

  return false;
}

void SokobanSolver::RecordVisited(const State& state, int state_index) {
  visited_table_[state.hash].push_back(state_index);
}

std::string SokobanSolver::ReconstructSolution(int goal_state_index) {
  std::vector<int> path_indices;
  int curr = goal_state_index;
  while (curr != 0) {
    path_indices.push_back(curr);
    curr = state_pool_[curr].parent_index;
  }

  std::reverse(path_indices.begin(), path_indices.end());

  int player_pos = board_.initial_player();

  for (int i = 0; i < board_.area(); ++i) {
    board_.set_blocked(i, board_.board(i) == '#');
  }

  for (int i = 0; i < board_.box_count(); ++i) {
    board_.set_blocked(board_.initial_boxes()[i], true);
  }

  std::string solution;

  for (int idx : path_indices) {
    const State& st = state_pool_[idx];
    const int target_stand = st.push_from;

    std::vector<int> parent(board_.area(), -1);
    std::vector<int> move_dir(board_.area(), -1);

    int head = 0, tail = 0;
    ++visited_seq_;
    bfs_q_[tail++] = player_pos;
    visited_[player_pos] = visited_seq_;

    while (head < tail) {
      const int c = bfs_q_[head++];
      if (c == target_stand) {
        break;
      }

      for (int d = 0; d < 4; ++d) {
        const int next = c + board_.dir_offset(d);
        if (!board_.blocked(next) && visited_[next] != visited_seq_) {
          visited_[next] = visited_seq_;
          parent[next] = c;
          move_dir[next] = d;
          bfs_q_[tail++] = next;
        }
      }
    }

    if (visited_[target_stand] != visited_seq_) {
      break;
    }

    std::vector<char> walk_moves;
    int crawl = target_stand;
    while (crawl != player_pos) {
      walk_moves.push_back(kMoveChars[move_dir[crawl]]);
      crawl = parent[crawl];
    }

    std::reverse(walk_moves.begin(), walk_moves.end());
    for (char ch : walk_moves) {
      solution.push_back(ch);
    }

    int box_pos = target_stand + board_.dir_offset(st.push_dir);
    for (int step = 0; step < st.macro_steps; ++step) {
      solution.push_back(kMoveChars[st.push_dir]);
      const int next_box = box_pos + board_.dir_offset(st.push_dir);
      board_.set_blocked(box_pos, false);
      board_.set_blocked(next_box, true);
      player_pos = box_pos;
      box_pos = next_box;
    }
  }

  return solution;
}

int SokobanSolver::ApplyTunnelMacro(int dest, int dir, int& out_macro_steps) const {
  int curr_box = dest;
  out_macro_steps = 1;
  const int perp1 = (dir < 2) ? kLeft : kUp;
  const int perp2 = (dir < 2) ? kRight : kDown;

  while (!board_.is_goal(curr_box)) {
    const int side1 = curr_box + board_.dir_offset(perp1);
    const int side2 = curr_box + board_.dir_offset(perp2);

    if (board_.board(side1) != '#' || board_.board(side2) != '#') {
      break;
    }

    const int next_box = curr_box + board_.dir_offset(dir);
    if (board_.blocked(next_box) ||
        (kUseDeadSquares && board_.min_goal_dist(next_box) >= kUnreachableDistance)) {
      break;
    }
    if (deadlock_.Check2x2(board_, next_box, curr_box)) {
      break;
    }

    curr_box = next_box;
    out_macro_steps++;
  }

  return curr_box;
}

int SokobanSolver::CalculateNextNormalizedPlayer(int dest, int dir, int orig_box_pos, 
                                                 int parent_norm, int perp1, int perp2) {
  const bool dest_was_reachable = (reach_[dest] == reach_seq_);
  const int side1 = orig_box_pos + board_.dir_offset(perp1);
  const int side2 = orig_box_pos + board_.dir_offset(perp2);

  bool fast_path = (board_.blocked(side1) || reach_[side1] == reach_seq_) && (board_.blocked(side2) || reach_[side2] == reach_seq_);
  if (fast_path && dest_was_reachable) {
    fast_path = deadlock_.DestNotCutVertex(board_, dest, reach_, reach_seq_);
  }

  if (fast_path) {
    const int region_min = (parent_norm != dest) ? parent_norm : region_second_norm_;
    return std::min(region_min, orig_box_pos);
  }

  if (visited_seq_ > 0xFF000000u) {
    visited_.fill(0);
    visited_seq_ = 0;
  }
  ++visited_seq_;

  int n_head = 0, n_tail = 0;
  const int player_land = dest - board_.dir_offset(dir);
  bfs_q_[n_tail++] = player_land;
  visited_[player_land] = visited_seq_;
  int next_norm = player_land;

  while (n_head < n_tail) {
    const int nc = bfs_q_[n_head++];
    if (nc < next_norm) {
      next_norm = nc;
    }

    for (int nd = 0; nd < 4; ++nd) {
      const int nn = nc + board_.dir_offset(nd);

      if (!board_.blocked(nn) && visited_[nn] != visited_seq_) {
        visited_[nn] = visited_seq_;
        bfs_q_[n_tail++] = nn;
      }
    }
  }

  return next_norm;
}

bool SokobanSolver::TryPushBox(const State& curr_state, int curr_index, int bi, int d, const PiCorralResult& corral, int parent_norm) {
  const int orig_box_pos = curr_state.boxes[bi];
  const int push_from = orig_box_pos - board_.dir_offset(d);
  if (reach_[push_from] != reach_seq_) {
    return false;
  }

  int dest = orig_box_pos + board_.dir_offset(d);
  if (corral.corral_id > 0 && deadlock_.corral_id(dest) != corral.corral_id) {
    return false;
  }
  if (board_.blocked(dest) || (kUseDeadSquares && board_.min_goal_dist(dest) >= kUnreachableDistance)) {
    return false;
  }
  if (deadlock_.Check2x2(board_, dest, orig_box_pos)) {
    return false;
  }

  int macro_steps = 1;
  if (kUseMacroMoves) {
    dest = ApplyTunnelMacro(dest, d, macro_steps);
  }

  if (!kUseFreezeCheck) {
  } else if (!board_.is_goal(dest)) {
    if (deadlock_.IsFrozen(board_, dest, dest, orig_box_pos)) {
      return false;
    }
  } else {
    bool adjacent_deadlock = false;
    for (int nd = 0; nd < 4; ++nd) {
      const int adj = dest + board_.dir_offset(nd);
      if (adj != orig_box_pos && board_.is_box(adj) && !board_.is_goal(adj)) {
        if (deadlock_.IsFrozen(board_, adj, dest, orig_box_pos)) {
          adjacent_deadlock = true;
          break;
        }
      }
    }
    if (adjacent_deadlock) {
      return false;
    }
  }

  State next_state;
  next_state.parent_index = curr_index;
  next_state.push_from = push_from;
  next_state.push_dir = d;
  next_state.macro_steps = macro_steps;
  next_state.g = curr_state.g + macro_steps;
  next_state.last_box_pos = dest;
  next_state.switches = curr_state.switches;
  if (curr_state.last_box_pos != 0 && curr_state.last_box_pos != orig_box_pos) {
    next_state.switches++;
  }

  next_state.hash = curr_state.hash ^ hasher_.box_key(orig_box_pos) ^ hasher_.box_key(dest) ^ hasher_.player_key(curr_state.player_cell);

  board_.set_box(orig_box_pos, false);
  board_.set_blocked(orig_box_pos, false);
  board_.set_box(dest, true);
  board_.set_blocked(dest, true);

  const int perp1 = (d < 2) ? kLeft : kUp;
  const int perp2 = (d < 2) ? kRight : kDown;
  const int next_norm = CalculateNextNormalizedPlayer(dest, d, orig_box_pos, parent_norm, perp1, perp2);

  next_state.player_cell = next_norm;
  next_state.hash ^= hasher_.player_key(next_norm);

  int moved_dst_idx = 0;
  for (int i = 0; i < board_.box_count(); ++i) {
    if (i != bi && curr_state.boxes[i] < dest) {
      ++moved_dst_idx;
    }
  }

  for (int dst = 0; dst < moved_dst_idx; ++dst) {
    const int src = (dst < bi) ? dst : dst + 1;
    next_state.boxes[dst] = curr_state.boxes[src];
    next_state.goal_of[dst] = curr_state.goal_of[src];
  }
  next_state.boxes[moved_dst_idx] = dest;
  for (int dst = moved_dst_idx + 1; dst < board_.box_count(); ++dst) {
    const int src = (dst <= bi) ? dst - 1 : dst;
    next_state.boxes[dst] = curr_state.boxes[src];
    next_state.goal_of[dst] = curr_state.goal_of[src];
  }

  if (IsVisited(next_state)) {
    board_.set_box(orig_box_pos, true);
    board_.set_blocked(orig_box_pos, true);
    board_.set_box(dest, false);
    board_.set_blocked(dest, false);
    return false;
  }

  const int h = CalculateHeuristicIncremental(board_, next_state.boxes, next_state.goal_of, moved_dst_idx, next_state.g);

  board_.set_box(orig_box_pos, true);
  board_.set_blocked(orig_box_pos, true);
  board_.set_box(dest, false);
  board_.set_blocked(dest, false);

  if (kUseDeadSquares && h >= kUnreachableDistance) {
    return false;
  }

  const int next_index = static_cast<int>(state_pool_.size());
  state_pool_.push_back(next_state);
  RecordVisited(next_state, next_index);

  if (h == 0) {
    solution_ = ReconstructSolution(next_index);
    g_solution_found.store(true);
    return true;
  }

  const uint64_t tier1 = kNoHeuristic ? 0 : static_cast<uint64_t>(h) * kHeuristicScale;
  const uint64_t tier2 = static_cast<uint64_t>(next_state.switches) * kSwitchPenalty;

  const uint64_t base_priority = tier1 + tier2;
  const uint64_t tie_breaker = 65535ULL - std::min<uint64_t>(next_state.g, 65535);

  open_set_.push({(base_priority << 16) | tie_breaker, next_index});
  return false;
}

bool SokobanSolver::ExpandCurrentState(const State& curr_state, int curr_index) {
  for (int i = 0; i < board_.box_count(); ++i) {
    board_.set_box(curr_state.boxes[i], true);
    board_.set_blocked(curr_state.boxes[i], true);
  }

  const int p_pos = (curr_index == 0)
                        ? board_.initial_player()
                        : curr_state.last_box_pos - board_.dir_offset(curr_state.push_dir);

  const int parent_norm = ComputeNormalizedPlayer(p_pos, true);

  const bool has_unreachable_floor = (region_size_ + board_.box_count() < static_cast<int>(board_.playable_cells().size()));

  PiCorralResult corral;
  if (kUsePiCorral && has_unreachable_floor) {
    corral = deadlock_.FindPiCorral(board_, region_size_, reach_, reach_seq_);
    if (corral.is_deadlock) {
      for (int i = 0; i < board_.box_count(); ++i) {
        board_.set_box(curr_state.boxes[i], false);
        board_.set_blocked(curr_state.boxes[i], false);
      }
      return false;
    }
  }

  if (kUseHallCheck && !deadlock_.HasFeasibleGoalAssignment(board_, curr_state.boxes, curr_state.goal_of)) {
    for (int i = 0; i < board_.box_count(); ++i) {
      board_.set_box(curr_state.boxes[i], false);
      board_.set_blocked(curr_state.boxes[i], false);
    }
    return false;
  }

  for (int bi = 0; bi < board_.box_count(); ++bi) {
    for (int d = 0; d < 4; ++d) {
      if (TryPushBox(curr_state, curr_index, bi, d, corral, parent_norm)) {
        return true;
      }
    }
  }

  for (int i = 0; i < board_.box_count(); ++i) {
    board_.set_box(curr_state.boxes[i], false);
    board_.set_blocked(curr_state.boxes[i], false);
  }

  return false;
}

void SokobanSolver::InitializeSearch() {
  deadlock_.Reset();
  state_pool_.clear();
  state_pool_.reserve(1'000'000);
  visited_table_.clear();
  visited_table_.reserve(1'000'000);
  while (!open_set_.empty()) open_set_.pop();

  State initial_state;
  for (int i = 0; i < board_.box_count(); ++i) {
    initial_state.boxes[i] = board_.initial_boxes()[i];
    board_.set_box(board_.initial_boxes()[i], true);
    board_.set_blocked(board_.initial_boxes()[i], true);
    initial_state.hash ^= hasher_.box_key(board_.initial_boxes()[i]);
  }

  initial_state.player_cell = ComputeNormalizedPlayer(board_.initial_player(), false);
  initial_state.hash ^= hasher_.player_key(initial_state.player_cell);

  int initial_h = CalculateHeuristic(board_, initial_state.boxes, initial_state.goal_of);
  if ((kUseDeadSquares && initial_h >= kUnreachableDistance) || initial_h == 0) {
    return;
  }

  state_pool_.push_back(initial_state);
  RecordVisited(initial_state, 0);
  open_set_.push({0, 0});

  for (int i = 0; i < board_.box_count(); ++i) {
    board_.set_box(board_.initial_boxes()[i], false);
    board_.set_blocked(board_.initial_boxes()[i], false);
  }
}

std::string SokobanSolver::Solve() {
  InitializeSearch();
  int iterations = 0;

  while (!open_set_.empty()) {
    if ((iterations++ & 127) == 0 && g_solution_found.load(std::memory_order_relaxed)) {
      return "";
    }

    const PriorityNode curr_node = open_set_.top();
    open_set_.pop();

    const int curr_index = curr_node.state_index;
    State curr_state = state_pool_[curr_index];
    g_nodes_explored.fetch_add(1, std::memory_order_relaxed);

    if (ExpandCurrentState(curr_state, curr_index)) {
      return solution_;
    }
  }

  return "";
}

char* DuplicateCString(const std::string& str) {
  char* buffer = static_cast<char*>(std::malloc(str.size() + 1));
  if (buffer) {
    std::memcpy(buffer, str.c_str(), str.size() + 1);
  }
  return buffer;
}

}  // namespace

char* SolveSokoban(int width, int height, char** map, char** items) {
  g_solution_found.store(false);
  g_nodes_explored.store(0);

  auto solver = std::make_unique<SokobanSolver>(width, height, map, items);
  std::string solution = solver->Solve();

  return DuplicateCString(solution);
}
