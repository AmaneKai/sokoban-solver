// Backward search: start from the solved board and pull boxes back to their starting cells.
#ifndef NATIVE_INCLUDE_SOKO_BACKWARD_H_
#define NATIVE_INCLUDE_SOKO_BACKWARD_H_

#include <atomic>
#include <string>

#include "flags.h"
#include "soko/board.h"

namespace soko {

struct BackwardResult {
  bool solved = false;
  std::string moves;  // forward move string, same format as the forward search
  u64 expansions = 0;
  bool pool_exhausted = false;
};

// Greedy best-first search over pull moves. Roots are the solved box layout with the player
// in each open region. A state is the goal when its boxes sit exactly on the initial box cells
// and the player shares a region with the initial player cell. The pulls, reversed, are a
// forward solution. Stops early when `stop` becomes true.
BackwardResult BackwardSearch(const Board& board, const ThreadConfig& config,
                              const std::atomic<bool>& stop);

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_BACKWARD_H_
