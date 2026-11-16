#ifndef NATIVE_INCLUDE_SOKOBAN_H_
#define NATIVE_INCLUDE_SOKOBAN_H_

#include <atomic>

#include "soko/types.h"

extern std::atomic<u64> g_nodes_explored;
extern std::atomic<bool> g_solution_found;
extern std::atomic<int> g_winning_thread;
extern std::atomic<bool> g_pool_exhausted;

// How each thread ended during the last SolveSokoban call, for the benchmark. Indexed by
// thread id; slots of threads that did not run stay kNotRun.
inline constexpr int kThreadSlots = 8;
enum class ThreadOutcome : u8 {
  kNotRun = 0,
  kRunning,
  kSolved,     // found a solution (it may have lost the race by a hair)
  kStopped,    // another thread won, or the caller timed out
  kExhausted,  // ran out of states to expand: the search space under its pruning is empty
  kPoolFull,   // ran out of state memory
  kHopeless,   // refused to start (initial position looked dead)
  kNoMemory,   // could not allocate its tables
};
extern std::atomic<u64> g_thread_nodes[kThreadSlots];
extern std::atomic<u8> g_thread_outcome[kThreadSlots];

char* SolveSokoban(int width, int height, char** map, char** items);

#ifdef SOKO_TRACE
enum class TraceKind : u8 { kPop, kChild, kSolution, kPruned, kNodePruned, kPhase };

enum class TracePhase : u8 {
  kFloodFill,
  kPiCorralSearch,
  kHallCheck,
  kPushChecks,
  kStateFill,
  kPlayerFlood,
  kBoxCopy,
  kHashInsert,
  kHeuristic,
};

enum class TraceReason : u8 {
  kNone,
  kOutsideCorral,
  kDeadSquare,
  kTwoByTwo,
  kFreeze,
  kDuplicate,
  kDeadHeuristic,
  kPiCorralDeadlock,
  kHallInfeasible,
};

struct TraceEvent {
  TraceKind kind;
  TraceReason reason;
  u32 node;
  u32 parent;
  int from_row, from_column, to_row, to_column;
  u32 g;
  u32 h;
};

extern int g_trace_thread_id;
extern void (*g_trace_event)(const TraceEvent& event);
#endif  // SOKO_TRACE

#endif  // NATIVE_INCLUDE_SOKOBAN_H_
